.. -*- coding: utf-8; mode: rst -*-

.. _dvb-frontend-parameters:

*******************
frontend parameters
*******************

The kind of parameters passed to the frontend device for tuning depend
on the kind of hardware you are using.

The struct ``dvb_frontend_parameters`` uses an union with specific
per-system parameters. However, as newer delivery systems required more
data, the structure size weren't enough to fit, and just extending its
size would break the existing applications. So, those parameters were
replaced by the usage of
:ref:`FE_GET_PROPERTY/FE_SET_PROPERTY <FE_GET_PROPERTY>`
ioctl's. The new API is flexible enough to add new parameters to
existing delivery systems, and to add newer delivery systems.

So, newer applications should use
:ref:`FE_GET_PROPERTY/FE_SET_PROPERTY <FE_GET_PROPERTY>`
instead, in order to be able to support the newer System Delivery like
DVB-S2, DVB-T2, DVB-C2, ISDB, etc.

All kinds of parameters are combined as an union in the
FrontendParameters structure:


.. code-block:: c

    struct dvb_frontend_parameters {
	uint32_t frequency;     /* (absolute) frequency in Hz for QAM/OFDM */
		    /* intermediate frequency in kHz for QPSK */
	fe_spectral_inversion_t inversion;
	union {
	    struct dvb_qpsk_parameters qpsk;
	    struct dvb_qam_parameters  qam;
	    struct dvb_ofdm_parameters ofdm;
	    struct dvb_vsb_parameters  vsb;
	} u;
    };

In the case of QPSK frontends the ``frequency`` field specifies the
intermediate frequency, i.e. the offset which is effectively added to
the local oscillator frequency (LOF) of the LNB. The intermediate
frequency has to be specified in units of kHz. For QAM and OFDM
frontends the ``frequency`` specifies the absolute frequency and is
given in Hz.


.. _dvb-qpsk-parameters:

QPSK parameters
===============

For satellite QPSK frontends you have to use the ``dvb_qpsk_parameters``
structure:


.. code-block:: c

     struct dvb_qpsk_parameters {
	 uint32_t        symbol_rate;  /* symbol rate in Symbols per second */
	 fe_code_rate_t  fec_inner;    /* forward error correction (see above) */
     };


.. _dvb-qam-parameters:

QAM parameters
==============

for cable QAM frontend you use the ``dvb_qam_parameters`` structure:


.. code-block:: c

     struct dvb_qam_parameters {
	 uint32_t         symbol_rate; /* symbol rate in Symbols per second */
	 fe_code_rate_t   fec_inner;   /* forward error correction (see above) */
	 fe_modulation_t  modulation;  /* modulation type (see above) */
     };


.. _dvb-vsb-parameters:

VSB parameters
==============

ATSC frontends are supported by the ``dvb_vsb_parameters`` structure:


.. code-block:: c

    struct dvb_vsb_parameters {
	fe_modulation_t modulation; /* modulation type (see above) */
    };


.. _dvb-ofdm-parameters:

OFDM parameters
===============

DVB-T frontends are supported by the ``dvb_ofdm_parameters`` structure:


.. code-block:: c

     struct dvb_ofdm_parameters {
	 fe_bandwidth_t      bandwidth;
	 fe_code_rate_t      code_rate_HP;  /* high priority stream code rate */
	 fe_code_rate_t      code_rate_LP;  /* low priority stream code rate */
	 fe_modulation_t     constellation; /* modulation type (see above) */
	 fe_transmit_mode_t  transmission_mode;
	 fe_guard_interval_t guard_interval;
	 fe_hierarchy_t      hierarchy_information;
     };
