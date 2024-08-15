.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _dvb_demux:

#######################
Digital TV Demux Device
#######################

The Digital TV demux device controls the MPEG-TS filters for the
digital TV. If the driver and hardware supports, those filters are
implemented at the hardware. Otherwise, the Kernel provides a software
emulation.

It can be accessed through ``/dev/adapter?/demux?``. Data types and
ioctl definitions can be accessed by including ``linux/dvb/dmx.h`` in
your application.


.. toctree::
    :maxdepth: 1

    dmx_types
    dmx_fcalls
