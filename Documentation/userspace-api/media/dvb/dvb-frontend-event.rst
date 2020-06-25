.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. c:type:: dvb_frontend_event

***************
frontend events
***************


.. code-block:: c

     struct dvb_frontend_event {
	 fe_status_t status;
	 struct dvb_frontend_parameters parameters;
     };
