.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

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
