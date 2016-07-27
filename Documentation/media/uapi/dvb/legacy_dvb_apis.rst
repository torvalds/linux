.. -*- coding: utf-8; mode: rst -*-

.. _legacy_dvb_apis:

*******************
DVB Deprecated APIs
*******************

The APIs described here are kept only for historical reasons. There's
just one driver for a very legacy hardware that uses this API. No modern
drivers should use it. Instead, audio and video should be using the V4L2
and ALSA APIs, and the pipelines should be set using the Media
Controller API


.. toctree::
    :maxdepth: 1

    video
    audio
