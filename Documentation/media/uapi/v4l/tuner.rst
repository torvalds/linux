.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _tuner:

*********************
Tuners and Modulators
*********************


Tuners
======

Video input devices can have one or more tuners demodulating a RF
signal. Each tuner is associated with one or more video inputs,
depending on the number of RF connectors on the tuner. The ``type``
field of the respective struct :c:type:`v4l2_input`
returned by the :ref:`VIDIOC_ENUMINPUT` ioctl is
set to ``V4L2_INPUT_TYPE_TUNER`` and its ``tuner`` field contains the
index number of the tuner.

Radio input devices have exactly one tuner with index zero, no video
inputs.

To query and change tuner properties applications use the
:ref:`VIDIOC_G_TUNER <VIDIOC_G_TUNER>` and
:ref:`VIDIOC_S_TUNER <VIDIOC_G_TUNER>` ioctls, respectively. The
struct :c:type:`v4l2_tuner` returned by :ref:`VIDIOC_G_TUNER <VIDIOC_G_TUNER>`
also contains signal status information applicable when the tuner of the
current video or radio input is queried.

.. note::

   :ref:`VIDIOC_S_TUNER <VIDIOC_G_TUNER>` does not switch the
   current tuner, when there is more than one. The tuner is solely
   determined by the current video input. Drivers must support both ioctls
   and set the ``V4L2_CAP_TUNER`` flag in the struct :c:type:`v4l2_capability`
   returned by the :ref:`VIDIOC_QUERYCAP` ioctl when the
   device has one or more tuners.


Modulators
==========

Video output devices can have one or more modulators, that modulate a
video signal for radiation or connection to the antenna input of a TV
set or video recorder. Each modulator is associated with one or more
video outputs, depending on the number of RF connectors on the
modulator. The ``type`` field of the respective struct
:c:type:`v4l2_output` returned by the
:ref:`VIDIOC_ENUMOUTPUT` ioctl is set to
``V4L2_OUTPUT_TYPE_MODULATOR`` and its ``modulator`` field contains the
index number of the modulator.

Radio output devices have exactly one modulator with index zero, no
video outputs.

A video or radio device cannot support both a tuner and a modulator. Two
separate device nodes will have to be used for such hardware, one that
supports the tuner functionality and one that supports the modulator
functionality. The reason is a limitation with the
:ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>` ioctl where you
cannot specify whether the frequency is for a tuner or a modulator.

To query and change modulator properties applications use the
:ref:`VIDIOC_G_MODULATOR <VIDIOC_G_MODULATOR>` and
:ref:`VIDIOC_S_MODULATOR <VIDIOC_G_MODULATOR>` ioctl. Note that
:ref:`VIDIOC_S_MODULATOR <VIDIOC_G_MODULATOR>` does not switch the current modulator, when there
is more than one at all. The modulator is solely determined by the
current video output. Drivers must support both ioctls and set the
``V4L2_CAP_MODULATOR`` flag in the struct
:c:type:`v4l2_capability` returned by the
:ref:`VIDIOC_QUERYCAP` ioctl when the device has
one or more modulators.


Radio Frequency
===============

To get and set the tuner or modulator radio frequency applications use
the :ref:`VIDIOC_G_FREQUENCY <VIDIOC_G_FREQUENCY>` and
:ref:`VIDIOC_S_FREQUENCY <VIDIOC_G_FREQUENCY>` ioctl which both take
a pointer to a struct :c:type:`v4l2_frequency`. These
ioctls are used for TV and radio devices alike. Drivers must support
both ioctls when the tuner or modulator ioctls are supported, or when
the device is a radio device.
