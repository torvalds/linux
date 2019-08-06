.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _dvb_video:

#######################
Digital TV Video Device
#######################

The Digital TV video device controls the MPEG2 video decoder of the Digital
TV hardware. It can be accessed through **/dev/dvb/adapter0/video0**. Data
types and and ioctl definitions can be accessed by including
**linux/dvb/video.h** in your application.

Note that the Digital TV video device only controls decoding of the MPEG video
stream, not its presentation on the TV or computer screen. On PCs this
is typically handled by an associated video4linux device, e.g.
**/dev/video**, which allows scaling and defining output windows.

Some Digital TV cards don’t have their own MPEG decoder, which results in the
omission of the audio and video device as well as the video4linux
device.

The ioctls that deal with SPUs (sub picture units) and navigation
packets are only supported on some MPEG decoders made for DVD playback.

These ioctls were also used by V4L2 to control MPEG decoders implemented
in V4L2. The use of these ioctls for that purpose has been made obsolete
and proper V4L2 ioctls or controls have been created to replace that
functionality.


.. toctree::
    :maxdepth: 1

    video_types
    video_function_calls
