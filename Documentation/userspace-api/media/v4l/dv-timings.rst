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

Applications can make use of the :ref:`input-capabilities` and
:ref:`output-capabilities` flags to determine whether the digital
video ioctls can be used with the given input or output.
