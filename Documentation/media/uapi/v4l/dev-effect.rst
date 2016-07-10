.. -*- coding: utf-8; mode: rst -*-

.. _effect:

************************
Effect Devices Interface
************************

.. note::
    This interface has been be suspended from the V4L2 API.
    The implementation for such effects should be done
    via mem2mem devices.

A V4L2 video effect device can do image effects, filtering, or combine
two or more images or image streams. For example video transitions or
wipes. Applications send data to be processed and receive the result
data either with :ref:`read() <func-read>` and
:ref:`write() <func-write>` functions, or through the streaming I/O
mechanism.

[to do]
