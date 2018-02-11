.. -*- coding: utf-8; mode: rst -*-

.. _dvb-fe-read-status:

***************************************
Querying frontend status and statistics
***************************************

Once :ref:`FE_SET_PROPERTY <FE_GET_PROPERTY>` is called, the
frontend will run a kernel thread that will periodically check for the
tuner lock status and provide statistics about the quality of the
signal.

The information about the frontend tuner locking status can be queried
using :ref:`FE_READ_STATUS`.

Signal statistics are provided via
:ref:`FE_GET_PROPERTY`.

.. note::

   Most statistics require the demodulator to be fully locked
   (e. g. with :c:type:`FE_HAS_LOCK <fe_status>` bit set). See
   :ref:`Frontend statistics indicators <frontend-stat-properties>` for
   more details.
