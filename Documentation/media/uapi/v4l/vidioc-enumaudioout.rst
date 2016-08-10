.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_ENUMAUDOUT:

***********************
ioctl VIDIOC_ENUMAUDOUT
***********************

Name
====

VIDIOC_ENUMAUDOUT - Enumerate audio outputs


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_audioout *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_ENUMAUDOUT

``argp``


Description
===========

To query the attributes of an audio output applications initialize the
``index`` field and zero out the ``reserved`` array of a struct
:ref:`v4l2_audioout <v4l2-audioout>` and call the ``VIDIOC_G_AUDOUT``
ioctl with a pointer to this structure. Drivers fill the rest of the
structure or return an ``EINVAL`` error code when the index is out of
bounds. To enumerate all audio outputs applications shall begin at index
zero, incrementing by one until the driver returns ``EINVAL``.

.. note:: Connectors on a TV card to loop back the received audio signal
    to a sound card are not audio outputs in this sense.

See :ref:`VIDIOC_G_AUDIOout <VIDIOC_G_AUDOUT>` for a description of struct
:ref:`v4l2_audioout <v4l2-audioout>`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The number of the audio output is out of bounds.
