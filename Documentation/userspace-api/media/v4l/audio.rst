.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _audio:

************************
Audio Inputs and Outputs
************************

Audio inputs and outputs are physical connectors of a device. Video
capture devices have inputs, output devices have outputs, zero or more
each. Radio devices have no audio inputs or outputs. They have exactly
one tuner which in fact *is* an audio source, but this API associates
tuners with video inputs or outputs only, and radio devices have none of
these. [#f1]_ A connector on a TV card to loop back the received audio
signal to a sound card is not considered an audio output.

Audio and video inputs and outputs are associated. Selecting a video
source also selects an audio source. This is most evident when the video
and audio source is a tuner. Further audio connectors can combine with
more than one video input or output. Assumed two composite video inputs
and two audio inputs exist, there may be up to four valid combinations.
The relation of video and audio connectors is defined in the
``audioset`` field of the respective struct
:c:type:`v4l2_input` or struct
:c:type:`v4l2_output`, where each bit represents the index
number, starting at zero, of one audio input or output.

To learn about the number and attributes of the available inputs and
outputs applications can enumerate them with the
:ref:`VIDIOC_ENUMAUDIO` and
:ref:`VIDIOC_ENUMAUDOUT <VIDIOC_ENUMAUDOUT>` ioctl, respectively.
The struct :c:type:`v4l2_audio` returned by the
:ref:`VIDIOC_ENUMAUDIO` ioctl also contains signal
status information applicable when the current audio input is queried.

The :ref:`VIDIOC_G_AUDIO <VIDIOC_G_AUDIO>` and
:ref:`VIDIOC_G_AUDOUT <VIDIOC_G_AUDOUT>` ioctls report the current
audio input and output, respectively.

.. note::

   Note that, unlike :ref:`VIDIOC_G_INPUT <VIDIOC_G_INPUT>` and
   :ref:`VIDIOC_G_OUTPUT <VIDIOC_G_OUTPUT>` these ioctls return a
   structure as :ref:`VIDIOC_ENUMAUDIO` and
   :ref:`VIDIOC_ENUMAUDOUT <VIDIOC_ENUMAUDOUT>` do, not just an index.

To select an audio input and change its properties applications call the
:ref:`VIDIOC_S_AUDIO <VIDIOC_G_AUDIO>` ioctl. To select an audio
output (which presently has no changeable properties) applications call
the :ref:`VIDIOC_S_AUDOUT <VIDIOC_G_AUDOUT>` ioctl.

Drivers must implement all audio input ioctls when the device has
multiple selectable audio inputs, all audio output ioctls when the
device has multiple selectable audio outputs. When the device has any
audio inputs or outputs the driver must set the ``V4L2_CAP_AUDIO`` flag
in the struct :c:type:`v4l2_capability` returned by
the :ref:`VIDIOC_QUERYCAP` ioctl.


Example: Information about the current audio input
==================================================

.. code-block:: c

    struct v4l2_audio audio;

    memset(&audio, 0, sizeof(audio));

    if (-1 == ioctl(fd, VIDIOC_G_AUDIO, &audio)) {
	perror("VIDIOC_G_AUDIO");
	exit(EXIT_FAILURE);
    }

    printf("Current input: %s\\n", audio.name);


Example: Switching to the first audio input
===========================================

.. code-block:: c

    struct v4l2_audio audio;

    memset(&audio, 0, sizeof(audio)); /* clear audio.mode, audio.reserved */

    audio.index = 0;

    if (-1 == ioctl(fd, VIDIOC_S_AUDIO, &audio)) {
	perror("VIDIOC_S_AUDIO");
	exit(EXIT_FAILURE);
    }

.. [#f1]
   Actually struct :c:type:`v4l2_audio` ought to have a
   ``tuner`` field like struct :c:type:`v4l2_input`, not
   only making the API more consistent but also permitting radio devices
   with multiple tuners.
