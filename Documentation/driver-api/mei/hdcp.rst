.. SPDX-License-Identifier: GPL-2.0

HDCP:
=====

ME FW as a security engine provides the capability for setting up
HDCP2.2 protocol negotiation between the Intel graphics device and
an HDC2.2 sink.

ME FW prepares HDCP2.2 negotiation parameters, signs and encrypts them
according the HDCP 2.2 spec. The Intel graphics sends the created blob
to the HDCP2.2 sink.

Similarly, the HDCP2.2 sink's response is transferred to ME FW
for decryption and verification.

Once all the steps of HDCP2.2 negotiation are completed,
upon request ME FW will configure the port as authenticated and supply
the HDCP encryption keys to Intel graphics hardware.


mei_hdcp driver
---------------
.. kernel-doc:: drivers/misc/mei/hdcp/mei_hdcp.c
    :doc: MEI_HDCP Client Driver

mei_hdcp api
------------

.. kernel-doc:: drivers/misc/mei/hdcp/mei_hdcp.c
    :functions:

