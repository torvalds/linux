.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: V4L

.. _VIDIOC_ENUMAUDOUT:

***********************
ioctl VIDIOC_ENUMAUDOUT
***********************

Name
====

VIDIOC_ENUMAUDOUT - Enumerate audio outputs

Synopsis
========

.. c:macro:: VIDIOC_ENUMAUDOUT

``int ioctl(int fd, VIDIOC_ENUMAUDOUT, struct v4l2_audioout *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    Pointer to struct :c:type:`v4l2_audioout`.

Description
===========

To query the attributes of an audio output applications initialize the
``index`` field and zero out the ``reserved`` array of a struct
:c:type:`v4l2_audioout` and call the ``VIDIOC_G_AUDOUT``
ioctl with a pointer to this structure. Drivers fill the rest of the
structure or return an ``EINVAL`` error code when the index is out of
bounds. To enumerate all audio outputs applications shall begin at index
zero, incrementing by one until the driver returns ``EINVAL``.

.. note::

    Connectors on a TV card to loop back the received audio signal
    to a sound card are not audio outputs in this sense.

See :ref:`VIDIOC_G_AUDIOout <VIDIOC_G_AUDOUT>` for a description of struct
:c:type:`v4l2_audioout`.

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The number of the audio output is out of bounds.
