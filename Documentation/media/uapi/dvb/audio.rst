.. -*- coding: utf-8; mode: rst -*-

.. _dvb_audio:

################
DVB Audio Device
################
The DVB audio device controls the MPEG2 audio decoder of the DVB
hardware. It can be accessed through ``/dev/dvb/adapter?/audio?``. Data
types and and ioctl definitions can be accessed by including
``linux/dvb/audio.h`` in your application.

Please note that some DVB cards don’t have their own MPEG decoder, which
results in the omission of the audio and video device.

These ioctls were also used by V4L2 to control MPEG decoders implemented
in V4L2. The use of these ioctls for that purpose has been made obsolete
and proper V4L2 ioctls or controls have been created to replace that
functionality.


.. toctree::
    :maxdepth: 1

    audio_data_types
    audio_function_calls
