.. -*- coding: utf-8; mode: rst -*-

.. _dvb_demux:

################
DVB Demux Device
################
The DVB demux device controls the filters of the DVB hardware/software.
It can be accessed through ``/dev/adapter?/demux?``. Data types and and
ioctl definitions can be accessed by including ``linux/dvb/dmx.h`` in
your application.


.. toctree::
    :maxdepth: 1

    dmx_types
    dmx_fcalls
