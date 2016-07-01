.. -*- coding: utf-8; mode: rst -*-

.. _video:

************************
Video Inputs and Outputs
************************

Video inputs and outputs are physical connectors of a device. These can
be for example RF connectors (antenna/cable), CVBS a.k.a. Composite
Video, S-Video or RGB connectors. Video and VBI capture devices have
inputs. Video and VBI output devices have outputs, at least one each.
Radio devices have no video inputs or outputs.

To learn about the number and attributes of the available inputs and
outputs applications can enumerate them with the
:ref:`VIDIOC_ENUMINPUT <vidioc-enuminput>` and
:ref:`VIDIOC_ENUMOUTPUT <vidioc-enumoutput>` ioctl, respectively. The
struct :ref:`v4l2_input <v4l2-input>` returned by the
:ref:`VIDIOC_ENUMINPUT <vidioc-enuminput>` ioctl also contains signal
:status information applicable when the current video input is queried.

The :ref:`VIDIOC_G_INPUT <vidioc-g-input>` and
:ref:`VIDIOC_G_OUTPUT <vidioc-g-output>` ioctls return the index of
the current video input or output. To select a different input or output
applications call the :ref:`VIDIOC_S_INPUT <vidioc-g-input>` and
:ref:`VIDIOC_S_OUTPUT <vidioc-g-output>` ioctls. Drivers must
implement all the input ioctls when the device has one or more inputs,
all the output ioctls when the device has one or more outputs.

.. code-block:: c
    :caption: Example 1: Information about the current video input

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


.. code-block:: c
    :caption: Example 2: Switching to the first video input

    int index;

    index = 0;

    if (-1 == ioctl(fd, VIDIOC_S_INPUT, &index)) {
        perror("VIDIOC_S_INPUT");
        exit(EXIT_FAILURE);
    }




.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
