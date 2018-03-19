SCTP SELinux Support
=====================

Security Hooks
===============

``Documentation/security/LSM-sctp.rst`` describes the following SCTP security
hooks with the SELinux specifics expanded below::

    security_sctp_assoc_request()
    security_sctp_bind_connect()
    security_sctp_sk_clone()
    security_inet_conn_established()


security_sctp_assoc_request()
-----------------------------
Passes the ``@ep`` and ``@chunk->skb`` of the association INIT packet to the
security module. Returns 0 on success, error on failure.
::

    @ep - pointer to sctp endpoint structure.
    @skb - pointer to skbuff of association packet.

The security module performs the following operations:
     IF this is the first association on ``@ep->base.sk``, then set the peer
     sid to that in ``@skb``. This will ensure there is only one peer sid
     assigned to ``@ep->base.sk`` that may support multiple associations.

     ELSE validate the ``@ep->base.sk peer_sid`` against the ``@skb peer sid``
     to determine whether the association should be allowed or denied.

     Set the sctp ``@ep sid`` to socket's sid (from ``ep->base.sk``) with
     MLS portion taken from ``@skb peer sid``. This will be used by SCTP
     TCP style sockets and peeled off connections as they cause a new socket
     to be generated.

     If IP security options are configured (CIPSO/CALIPSO), then the ip
     options are set on the socket.


security_sctp_bind_connect()
-----------------------------
Checks permissions required for ipv4/ipv6 addresses based on the ``@optname``
as follows::

  ------------------------------------------------------------------
  |                   BIND Permission Checks                       |
  |       @optname             |         @address contains         |
  |----------------------------|-----------------------------------|
  | SCTP_SOCKOPT_BINDX_ADD     | One or more ipv4 / ipv6 addresses |
  | SCTP_PRIMARY_ADDR          | Single ipv4 or ipv6 address       |
  | SCTP_SET_PEER_PRIMARY_ADDR | Single ipv4 or ipv6 address       |
  ------------------------------------------------------------------

  ------------------------------------------------------------------
  |                 CONNECT Permission Checks                      |
  |       @optname             |         @address contains         |
  |----------------------------|-----------------------------------|
  | SCTP_SOCKOPT_CONNECTX      | One or more ipv4 / ipv6 addresses |
  | SCTP_PARAM_ADD_IP          | One or more ipv4 / ipv6 addresses |
  | SCTP_SENDMSG_CONNECT       | Single ipv4 or ipv6 address       |
  | SCTP_PARAM_SET_PRIMARY     | Single ipv4 or ipv6 address       |
  ------------------------------------------------------------------


``Documentation/security/LSM-sctp.rst`` gives a summary of the ``@optname``
entries and also describes ASCONF chunk processing when Dynamic Address
Reconfiguration is enabled.


security_sctp_sk_clone()
-------------------------
Called whenever a new socket is created by **accept**\(2) (i.e. a TCP style
socket) or when a socket is 'peeled off' e.g userspace calls
**sctp_peeloff**\(3). ``security_sctp_sk_clone()`` will set the new
sockets sid and peer sid to that contained in the ``@ep sid`` and
``@ep peer sid`` respectively.
::

    @ep - pointer to current sctp endpoint structure.
    @sk - pointer to current sock structure.
    @sk - pointer to new sock structure.


security_inet_conn_established()
---------------------------------
Called when a COOKIE ACK is received where it sets the connection's peer sid
to that in ``@skb``::

    @sk  - pointer to sock structure.
    @skb - pointer to skbuff of the COOKIE ACK packet.


Policy Statements
==================
The following class and permissions to support SCTP are available within the
kernel::

    class sctp_socket inherits socket { node_bind }

whenever the following policy capability is enabled::

    policycap extended_socket_class;

SELinux SCTP support adds the ``name_connect`` permission for connecting
to a specific port type and the ``association`` permission that is explained
in the section below.

If userspace tools have been updated, SCTP will support the ``portcon``
statement as shown in the following example::

    portcon sctp 1024-1036 system_u:object_r:sctp_ports_t:s0


SCTP Peer Labeling
===================
An SCTP socket will only have one peer label assigned to it. This will be
assigned during the establishment of the first association. Any further
associations on this socket will have their packet peer label compared to
the sockets peer label, and only if they are different will the
``association`` permission be validated. This is validated by checking the
socket peer sid against the received packets peer sid to determine whether
the association should be allowed or denied.

NOTES:
   1) If peer labeling is not enabled, then the peer context will always be
      ``SECINITSID_UNLABELED`` (``unlabeled_t`` in Reference Policy).

   2) As SCTP can support more than one transport address per endpoint
      (multi-homing) on a single socket, it is possible to configure policy
      and NetLabel to provide different peer labels for each of these. As the
      socket peer label is determined by the first associations transport
      address, it is recommended that all peer labels are consistent.

   3) **getpeercon**\(3) may be used by userspace to retrieve the sockets peer
      context.

   4) While not SCTP specific, be aware when using NetLabel that if a label
      is assigned to a specific interface, and that interface 'goes down',
      then the NetLabel service will remove the entry. Therefore ensure that
      the network startup scripts call **netlabelctl**\(8) to set the required
      label (see **netlabel-config**\(8) helper script for details).

   5) The NetLabel SCTP peer labeling rules apply as discussed in the following
      set of posts tagged "netlabel" at: http://www.paul-moore.com/blog/t.

   6) CIPSO is only supported for IPv4 addressing: ``socket(AF_INET, ...)``
      CALIPSO is only supported for IPv6 addressing: ``socket(AF_INET6, ...)``

      Note the following when testing CIPSO/CALIPSO:
         a) CIPSO will send an ICMP packet if an SCTP packet cannot be
            delivered because of an invalid label.
         b) CALIPSO does not send an ICMP packet, just silently discards it.

   7) IPSEC is not supported as RFC 3554 - sctp/ipsec support has not been
      implemented in userspace (**racoon**\(8) or **ipsec_pluto**\(8)),
      although the kernel supports SCTP/IPSEC.
