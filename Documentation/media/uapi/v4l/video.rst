.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _video:

************************
Video Inputs and Outputs
************************

Video inputs and outputs are physical connectors of a device. These can
be for example: RF connectors (antenna/cable), CVBS a.k.a. Composite
Video, S-Video and RGB connectors. Camera sensors are also considered to
be a video input. Video and VBI capture devices have inputs. Video and
VBI output devices have outputs, at least one each. Radio devices have
no video inputs or outputs.

To learn about the number and attributes of the available inputs and
outputs applications can enumerate them with the
:ref:`VIDIOC_ENUMINPUT` and
:ref:`VIDIOC_ENUMOUTPUT` ioctl, respectively. The
struct :c:type:`v4l2_input` returned by the
:ref:`VIDIOC_ENUMINPUT` ioctl also contains signal
status information applicable when the current video input is queried.

The :ref:`VIDIOC_G_INPUT <VIDIOC_G_INPUT>` and
:ref:`VIDIOC_G_OUTPUT <VIDIOC_G_OUTPUT>` ioctls return the index of
the current video input or output. To select a different input or output
applications call the :ref:`VIDIOC_S_INPUT <VIDIOC_G_INPUT>` and
:ref:`VIDIOC_S_OUTPUT <VIDIOC_G_OUTPUT>` ioctls. Drivers must
implement all the input ioctls when the device has one or more inputs,
all the output ioctls when the device has one or more outputs.

Example: Information about the current video input
==================================================

.. code-block:: c

    struct v4l2_input input;
    int index;

    if (-1 == ioctl(fd, VIDIOC_G_INPUT, &index)) {
	perror("VIDIOC_G_INPUT");
	exit(EXIT_FAILURE);
    }

    memset(&input, 0, sizeof(input));
    input.index = index;

    if (-1 == ioctl(fd, VIDIOC_ENUMINPUT, &input)) {
	perror("VIDIOC_ENUMINPUT");
	exit(EXIT_FAILURE);
    }

    printf("Current input: %s\\n", input.name);


Example: Switching to the first video input
===========================================

.. code-block:: c

    int index;

    index = 0;

    if (-1 == ioctl(fd, VIDIOC_S_INPUT, &index)) {
	perror("VIDIOC_S_INPUT");
	exit(EXIT_FAILURE);
    }
