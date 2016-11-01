.. -*- coding: utf-8; mode: rst -*-

.. _common:

###################
Common API Elements
###################
Programming a V4L2 device consists of these steps:

-  Opening the device

-  Changing device properties, selecting a video and audio input, video
   standard, picture brightness a. o.

-  Negotiating a data format

-  Negotiating an input/output method

-  The actual input/output loop

-  Closing the device

In practice most steps are optional and can be executed out of order. It
depends on the V4L2 device type, you can read about the details in
:ref:`devices`. In this chapter we will discuss the basic concepts
applicable to all devices.


.. toctree::
    :maxdepth: 1

    open
    querycap
    app-pri
    video
    audio
    tuner
    standard
    dv-timings
    control
    extended-controls
    format
    planar-apis
    crop
    selection-api
    streaming-par
