.. -*- coding: utf-8; mode: rst -*-

.. _codec:

***************
Codec Interface
***************

A V4L2 codec can compress, decompress, transform, or otherwise convert
video data from one format into another format, in memory. Typically
such devices are memory-to-memory devices (i.e. devices with the
``V4L2_CAP_VIDEO_M2M`` or ``V4L2_CAP_VIDEO_M2M_MPLANE`` capability set).

A memory-to-memory video node acts just like a normal video node, but it
supports both output (sending frames from memory to the codec hardware)
and capture (receiving the processed frames from the codec hardware into
memory) stream I/O. An application will have to setup the stream I/O for
both sides and finally call :ref:`VIDIOC_STREAMON <VIDIOC_STREAMON>`
for both capture and output to start the codec.

Video compression codecs use the MPEG controls to setup their codec
parameters

.. note:: The MPEG controls actually support many more codecs than
   just MPEG. See :ref:`mpeg-controls`.

Memory-to-memory devices can often be used as a shared resource: you can
open the video node multiple times, each application setting up their
own codec properties that are local to the file handle, and each can use
it independently from the others. The driver will arbitrate access to
the codec and reprogram it whenever another file handler gets access.
This is different from the usual video node behavior where the video
properties are global to the device (i.e. changing something through one
file handle is visible through another file handle).
