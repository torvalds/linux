.. SPDX-License-Identifier: GPL-2.0

=======================
In-Kernel TLS Handshake
=======================

Overview
========

Transport Layer Security (TLS) is a Upper Layer Protocol (ULP) that runs
over TCP. TLS provides end-to-end data integrity and confidentiality in
addition to peer authentication.

The kernel's kTLS implementation handles the TLS record subprotocol, but
does not handle the TLS handshake subprotocol which is used to establish
a TLS session. Kernel consumers can use the API described here to
request TLS session establishment.

There are several possible ways to provide a handshake service in the
kernel. The API described here is designed to hide the details of those
implementations so that in-kernel TLS consumers do not need to be
aware of how the handshake gets done.


User handshake agent
====================

As of this writing, there is no TLS handshake implementation in the
Linux kernel. To provide a handshake service, a handshake agent
(typically in user space) is started in each network namespace where a
kernel consumer might require a TLS handshake. Handshake agents listen
for events sent from the kernel that indicate a handshake request is
waiting.

An open socket is passed to a handshake agent via a netlink operation,
which creates a socket descriptor in the agent's file descriptor table.
If the handshake completes successfully, the handshake agent promotes
the socket to use the TLS ULP and sets the session information using the
SOL_TLS socket options. The handshake agent returns the socket to the
kernel via a second netlink operation.


Kernel Handshake API
====================

A kernel TLS consumer initiates a client-side TLS handshake on an open
socket by invoking one of the tls_client_hello() functions. First, it
fills in a structure that contains the parameters of the request:

.. code-block:: c

  struct tls_handshake_args {
        struct socket   *ta_sock;
        tls_done_func_t ta_done;
        void            *ta_data;
        const char      *ta_peername;
        unsigned int    ta_timeout_ms;
        key_serial_t    ta_keyring;
        key_serial_t    ta_my_cert;
        key_serial_t    ta_my_privkey;
        unsigned int    ta_num_peerids;
        key_serial_t    ta_my_peerids[5];
  };

The @ta_sock field references an open and connected socket. The consumer
must hold a reference on the socket to prevent it from being destroyed
while the handshake is in progress. The consumer must also have
instantiated a struct file in sock->file.


@ta_done contains a callback function that is invoked when the handshake
has completed. Further explanation of this function is in the "Handshake
Completion" sesction below.

The consumer can provide a NUL-terminated hostname in the @ta_peername
field that is sent as part of ClientHello. If no peername is provided,
the DNS hostname associated with the server's IP address is used instead.

The consumer can fill in the @ta_timeout_ms field to force the servicing
handshake agent to exit after a number of milliseconds. This enables the
socket to be fully closed once both the kernel and the handshake agent
have closed their endpoints.

Authentication material such as x.509 certificates, private certificate
keys, and pre-shared keys are provided to the handshake agent in keys
that are instantiated by the consumer before making the handshake
request. The consumer can provide a private keyring that is linked into
the handshake agent's process keyring in the @ta_keyring field to prevent
access of those keys by other subsystems.

To request an x.509-authenticated TLS session, the consumer fills in
the @ta_my_cert and @ta_my_privkey fields with the serial numbers of
keys containing an x.509 certificate and the private key for that
certificate. Then, it invokes this function:

.. code-block:: c

  ret = tls_client_hello_x509(args, gfp_flags);

The function returns zero when the handshake request is under way. A
zero return guarantees the callback function @ta_done will be invoked
for this socket. The function returns a negative errno if the handshake
could not be started. A negative errno guarantees the callback function
@ta_done will not be invoked on this socket.


To initiate a client-side TLS handshake with a pre-shared key, use:

.. code-block:: c

  ret = tls_client_hello_psk(args, gfp_flags);

However, in this case, the consumer fills in the @ta_my_peerids array
with serial numbers of keys containing the peer identities it wishes
to offer, and the @ta_num_peerids field with the number of array
entries it has filled in. The other fields are filled in as above.


To initiate an anonymous client-side TLS handshake use:

.. code-block:: c

  ret = tls_client_hello_anon(args, gfp_flags);

The handshake agent presents no peer identity information to the remote
during this type of handshake. Only server authentication (ie the client
verifies the server's identity) is performed during the handshake. Thus
the established session uses encryption only.


Consumers that are in-kernel servers use:

.. code-block:: c

  ret = tls_server_hello_x509(args, gfp_flags);

or

.. code-block:: c

  ret = tls_server_hello_psk(args, gfp_flags);

The argument structure is filled in as above.


If the consumer needs to cancel the handshake request, say, due to a ^C
or other exigent event, the consumer can invoke:

.. code-block:: c

  bool tls_handshake_cancel(sock);

This function returns true if the handshake request associated with
@sock has been canceled. The consumer's handshake completion callback
will not be invoked. If this function returns false, then the consumer's
completion callback has already been invoked.


Handshake Completion
====================

When the handshake agent has completed processing, it notifies the
kernel that the socket may be used by the consumer again. At this point,
the consumer's handshake completion callback, provided in the @ta_done
field in the tls_handshake_args structure, is invoked.

The synopsis of this function is:

.. code-block:: c

  typedef void	(*tls_done_func_t)(void *data, int status,
                                   key_serial_t peerid);

The consumer provides a cookie in the @ta_data field of the
tls_handshake_args structure that is returned in the @data parameter of
this callback. The consumer uses the cookie to match the callback to the
thread waiting for the handshake to complete.

The success status of the handshake is returned via the @status
parameter:

+------------+----------------------------------------------+
|  status    |  meaning                                     |
+============+==============================================+
|  0         |  TLS session established successfully        |
+------------+----------------------------------------------+
|  -EACCESS  |  Remote peer rejected the handshake or       |
|            |  authentication failed                       |
+------------+----------------------------------------------+
|  -ENOMEM   |  Temporary resource allocation failure       |
+------------+----------------------------------------------+
|  -EINVAL   |  Consumer provided an invalid argument       |
+------------+----------------------------------------------+
|  -ENOKEY   |  Missing authentication material             |
+------------+----------------------------------------------+
|  -EIO      |  An unexpected fault occurred                |
+------------+----------------------------------------------+

The @peerid parameter contains the serial number of a key containing the
remote peer's identity or the value TLS_NO_PEERID if the session is not
authenticated.

A best practice is to close and destroy the socket immediately if the
handshake failed.


Other considerations
--------------------

While a handshake is under way, the kernel consumer must alter the
socket's sk_data_ready callback function to ignore all incoming data.
Once the handshake completion callback function has been invoked, normal
receive operation can be resumed.

Once a TLS session is established, the consumer must provide a buffer
for and then examine the control message (CMSG) that is part of every
subsequent sock_recvmsg(). Each control message indicates whether the
received message data is TLS record data or session metadata.

See tls.rst for details on how a kTLS consumer recognizes incoming
(decrypted) application data, alerts, and handshake packets once the
socket has been promoted to use the TLS ULP.
