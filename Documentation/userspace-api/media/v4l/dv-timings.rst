.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _dv-timings:

**************************
Digital Video (DV) Timings
**************************

The video standards discussed so far have been dealing with Analog TV
and the corresponding video timings. Today there are many more different
hardware interfaces such as High Definition TV interfaces (HDMI), VGA,
DVI connectors etc., that carry video signals and there is a need to
extend the API to select the video timings for these interfaces. Since
it is not possible to extend the :ref:`v4l2_std_id <v4l2-std-id>`
due to the limited bits available, a new set of ioctls was added to
set/get video timings at the input and output.

These ioctls deal with the detailed digital video timings that define
each video format. This includes parameters such as the active video
width and height, signal polarities, frontporches, backporches, sync
widths etc. The ``linux/v4l2-dv-timings.h`` header can be used to get
the timings of the formats in the :ref:`cea861` and :ref:`vesadmt`
standards.

To enumerate and query the attributes of the DV timings supported by a
device applications use the
:ref:`VIDIOC_ENUM_DV_TIMINGS` and
:ref:`VIDIOC_DV_TIMINGS_CAP` ioctls. To set
DV timings for the device applications use the
:ref:`VIDIOC_S_DV_TIMINGS <VIDIOC_G_DV_TIMINGS>` ioctl and to get
current DV timings they use the
:ref:`VIDIOC_G_DV_TIMINGS <VIDIOC_G_DV_TIMINGS>` ioctl. To detect
the DV timings as seen by the video receiver applications use the
:ref:`VIDIOC_QUERY_DV_TIMINGS` ioctl.

When the hardware detects a video source change (e.g. the video
signal appears or disappears, or the video resolution changes), then
it will issue a `V4L2_EVENT_SOURCE_CHANGE` event. Use the
:ref:`ioctl VIDIOC_SUBSCRIBE_EVENT <VIDIOC_SUBSCRIBE_EVENT>` and the
:ref:`VIDIOC_DQEVENT` to check if this event was reported.

If the video signal changed, then the application has to stop
streaming, free all buffers, and call the :ref:`VIDIOC_QUERY_DV_TIMINGS`
to obtain the new video timings, and if they are valid, it can set
those by calling the :ref:`ioctl VIDIOC_S_DV_TIMINGS <VIDIOC_G_DV_TIMINGS>`.
This will also update the format, so use the :ref:`ioctl VIDIOC_G_FMT <VIDIOC_G_FMT>`
to obtain the new format. Now the application can allocate new buffers
and start streaming again.

The :ref:`VIDIOC_QUERY_DV_TIMINGS` will just report what the
hardware detects, it will never change the configuration. If the
currently set timings and the actually detected timings differ, then
typically this will mean that you will not be able to capture any
video. The correct approach is to rely on the `V4L2_EVENT_SOURCE_CHANGE`
event so you know when something changed.

Applications can make use of the :ref:`input-capabilities` and
:ref:`output-capabilities` flags to determine whether the digital
video ioctls can be used with the given input or output.
