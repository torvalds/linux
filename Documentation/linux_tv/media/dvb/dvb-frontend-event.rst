.. -*- coding: utf-8; mode: rst -*-

.. _dvb-frontend-event:

***************
frontend events
***************


.. code-block:: c

     struct dvb_frontend_event {
         fe_status_t status;
         struct dvb_frontend_parameters parameters;
     };




.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
