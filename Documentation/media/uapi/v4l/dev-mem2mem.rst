.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _mem2mem:

********************************
Video Memory-To-Memory Interface
********************************

A V4L2 memory-to-memory device can compress, decompress, transform, or
otherwise convert video data from one format into another format, in memory.
Such memory-to-memory devices set the ``V4L2_CAP_VIDEO_M2M`` or
``V4L2_CAP_VIDEO_M2M_MPLANE`` capability. Examples of memory-to-memory
devices are codecs, scalers, deinterlacers or format converters (i.e.
converting from YUV to RGB).

A memory-to-memory video node acts just like a normal video node, but it
supports both output (sending frames from memory to the hardware)
and capture (receiving the processed frames from the hardware into
memory) stream I/O. An application will have to setup the stream I/O for
both sides and finally call :ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>`
for both capture and output to start the hardware.

Memory-to-memory devices function as a shared resource: you can
open the video node multiple times, each application setting up their
own properties that are local to the file handle, and each can use
it independently from the others. The driver will arbitrate access to
the hardware and reprogram it whenever another file handler gets access.
This is different from the usual video node behavior where the video
properties are global to the device (i.e. changing something through one
file handle is visible through another file handle).

One of the most common memory-to-memory device is the codec. Codecs
are more complicated than most and require additional setup for
their codec parameters. This is done through codec controls.
See :ref:`mpeg-controls`. More details on how to use codec memory-to-memory
devices are given in the following sections.

.. toctree::
    :maxdepth: 1

    dev-decoder
    dev-stateless-decoder
