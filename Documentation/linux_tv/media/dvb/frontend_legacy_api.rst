.. -*- coding: utf-8; mode: rst -*-

.. _frontend_legacy_types:

Frontend Legacy Data Types
==========================


.. toctree::
    :maxdepth: 1

    fe-type-t
    fe-bandwidth-t
    dvb-frontend-parameters
    dvb-frontend-event


.. _frontend_legacy_fcalls:

Frontend Legacy Function Calls
==============================

Those functions are defined at DVB version 3. The support is kept in the
kernel due to compatibility issues only. Their usage is strongly not
recommended


.. toctree::
    :maxdepth: 1

    FE_READ_BER
    FE_READ_SNR
    FE_READ_SIGNAL_STRENGTH
    FE_READ_UNCORRECTED_BLOCKS
    FE_SET_FRONTEND
    FE_GET_FRONTEND
    FE_GET_EVENT
    FE_DISHNETWORK_SEND_LEGACY_CMD
