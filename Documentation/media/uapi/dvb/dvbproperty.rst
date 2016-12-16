.. -*- coding: utf-8; mode: rst -*-

.. _frontend-properties:

DVB Frontend properties
=======================

Tuning into a Digital TV physical channel and starting decoding it
requires changing a set of parameters, in order to control the tuner,
the demodulator, the Linear Low-noise Amplifier (LNA) and to set the
antenna subsystem via Satellite Equipment Control (SEC), on satellite
systems. The actual parameters are specific to each particular digital
TV standards, and may change as the digital TV specs evolves.

In the past, the strategy used was to have a union with the parameters
needed to tune for DVB-S, DVB-C, DVB-T and ATSC delivery systems grouped
there. The problem is that, as the second generation standards appeared,
those structs were not big enough to contain the additional parameters.
Also, the union didn't have any space left to be expanded without
breaking userspace. So, the decision was to deprecate the legacy
union/struct based approach, in favor of a properties set approach.

.. note::

   On Linux DVB API version 3, setting a frontend were done via
   struct :c:type:`dvb_frontend_parameters`.
   This got replaced on version 5 (also called "S2API", as this API were
   added originally_enabled to provide support for DVB-S2), because the
   old API has a very limited support to new standards and new hardware.
   This section describes the new and recommended way to set the frontend,
   with suppports all digital TV delivery systems.

Example: with the properties based approach, in order to set the tuner
to a DVB-C channel at 651 kHz, modulated with 256-QAM, FEC 3/4 and
symbol rate of 5.217 Mbauds, those properties should be sent to
:ref:`FE_SET_PROPERTY <FE_GET_PROPERTY>` ioctl:

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>` =
   SYS_DVBC_ANNEX_A

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>` = 651000000

-  :ref:`DTV_MODULATION <DTV-MODULATION>` = QAM_256

-  :ref:`DTV_INVERSION <DTV-INVERSION>` = INVERSION_AUTO

-  :ref:`DTV_SYMBOL_RATE <DTV-SYMBOL-RATE>` = 5217000

-  :ref:`DTV_INNER_FEC <DTV-INNER-FEC>` = FEC_3_4

-  :ref:`DTV_TUNE <DTV-TUNE>`

The code that would that would do the above is show in
:ref:`dtv-prop-example`.

.. _dtv-prop-example:

Example: Setting digital TV frontend properties
===============================================

.. code-block:: c

    #include <stdio.h>
    #include <fcntl.h>
    #include <sys/ioctl.h>
    #include <linux/dvb/frontend.h>

    static struct dtv_property props[] = {
	{ .cmd = DTV_DELIVERY_SYSTEM, .u.data = SYS_DVBC_ANNEX_A },
	{ .cmd = DTV_FREQUENCY,       .u.data = 651000000 },
	{ .cmd = DTV_MODULATION,      .u.data = QAM_256 },
	{ .cmd = DTV_INVERSION,       .u.data = INVERSION_AUTO },
	{ .cmd = DTV_SYMBOL_RATE,     .u.data = 5217000 },
	{ .cmd = DTV_INNER_FEC,       .u.data = FEC_3_4 },
	{ .cmd = DTV_TUNE }
    };

    static struct dtv_properties dtv_prop = {
	.num = 6, .props = props
    };

    int main(void)
    {
	int fd = open("/dev/dvb/adapter0/frontend0", O_RDWR);

	if (!fd) {
	    perror ("open");
	    return -1;
	}
	if (ioctl(fd, FE_SET_PROPERTY, &dtv_prop) == -1) {
	    perror("ioctl");
	    return -1;
	}
	printf("Frontend set\\n");
	return 0;
    }

.. attention:: While it is possible to directly call the Kernel code like the
   above example, it is strongly recommended to use
   `libdvbv5 <https://linuxtv.org/docs/libdvbv5/index.html>`__, as it
   provides abstraction to work with the supported digital TV standards and
   provides methods for usual operations like program scanning and to
   read/write channel descriptor files.


.. toctree::
    :maxdepth: 1

    dtv-stats
    dtv-fe-stats
    dtv-property
    dtv-properties
    dvbproperty-006
    fe_property_parameters
    frontend-stat-properties
    frontend-property-terrestrial-systems
    frontend-property-cable-systems
    frontend-property-satellite-systems
