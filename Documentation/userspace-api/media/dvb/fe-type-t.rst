.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

*************
Frontend type
*************

For historical reasons, frontend types are named by the type of
modulation used in transmission. The fontend types are given by
fe_type_t type, defined as:


.. c:type:: fe_type

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Frontend types
    :header-rows:  1
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  fe_type

       -  Description

       -  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>` equivalent
	  type

    -  .. row 2

       -  .. _FE-QPSK:

	  ``FE_QPSK``

       -  For DVB-S standard

       -  ``SYS_DVBS``

    -  .. row 3

       -  .. _FE-QAM:

	  ``FE_QAM``

       -  For DVB-C annex A standard

       -  ``SYS_DVBC_ANNEX_A``

    -  .. row 4

       -  .. _FE-OFDM:

	  ``FE_OFDM``

       -  For DVB-T standard

       -  ``SYS_DVBT``

    -  .. row 5

       -  .. _FE-ATSC:

	  ``FE_ATSC``

       -  For ATSC standard (terrestrial) or for DVB-C Annex B (cable) used
	  in US.

       -  ``SYS_ATSC`` (terrestrial) or ``SYS_DVBC_ANNEX_B`` (cable)


Newer formats like DVB-S2, ISDB-T, ISDB-S and DVB-T2 are not described
at the above, as they're supported via the new
:ref:`FE_GET_PROPERTY/FE_GET_SET_PROPERTY <FE_GET_PROPERTY>`
ioctl's, using the :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`
parameter.

In the old days, struct :c:type:`dvb_frontend_info`
used to contain ``fe_type_t`` field to indicate the delivery systems,
filled with either ``FE_QPSK, FE_QAM, FE_OFDM`` or ``FE_ATSC``. While this
is still filled to keep backward compatibility, the usage of this field
is deprecated, as it can report just one delivery system, but some
devices support multiple delivery systems. Please use
:ref:`DTV_ENUM_DELSYS <DTV-ENUM-DELSYS>` instead.

On devices that support multiple delivery systems, struct
:c:type:`dvb_frontend_info`::``fe_type_t`` is
filled with the currently standard, as selected by the last call to
:ref:`FE_SET_PROPERTY <FE_GET_PROPERTY>` using the
:ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>` property.
