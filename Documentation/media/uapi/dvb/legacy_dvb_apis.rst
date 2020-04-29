.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _legacy_dvb_apis:

***************************
Digital TV Deprecated APIs
***************************

The APIs described here **should not** be used on new drivers or applications.

The DVBv3 frontend API has issues with new delivery systems, including
DVB-S2, DVB-T2, ISDB, etc.

There's just one driver for a very legacy hardware using the Digital TV
audio and video APIs. No modern drivers should use it. Instead, audio and
video should be using the V4L2 and ALSA APIs, and the pipelines should
be set via the Media Controller API.

.. attention::

   The APIs described here doesn't necessarily reflect the current
   code implementation, as this section of the document was written
   for DVB version 1, while the code reflects DVB version 3
   implementation.


.. toctree::
    :maxdepth: 1

    frontend_legacy_dvbv3_api
    video
    audio
