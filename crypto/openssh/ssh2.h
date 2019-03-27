/* $OpenBSD: ssh2.h,v 1.18 2016/05/04 14:22:33 markus Exp $ */

/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * draft-ietf-secsh-architecture-05.txt
 *
 *   Transport layer protocol:
 *
 *     1-19     Transport layer generic (e.g. disconnect, ignore, debug,
 *              etc)
 *     20-29    Algorithm negotiation
 *     30-49    Key exchange method specific (numbers can be reused for
 *              different authentication methods)
 *
 *   User authentication protocol:
 *
 *     50-59    User authentication generic
 *     60-79    User authentication method specific (numbers can be reused
 *              for different authentication methods)
 *
 *   Connection protocol:
 *
 *     80-89    Connection protocol generic
 *     90-127   Channel related messages
 *
 *   Reserved for client protocols:
 *
 *     128-191  Reserved
 *
 *   Local extensions:
 *
 *     192-255  Local extensions
 */

/* special marker for no message */

#define SSH_MSG_NONE					0

/* ranges */

#define SSH2_MSG_TRANSPORT_MIN				1
#define SSH2_MSG_TRANSPORT_MAX				49
#define SSH2_MSG_USERAUTH_MIN				50
#define SSH2_MSG_USERAUTH_MAX				79
#define SSH2_MSG_USERAUTH_PER_METHOD_MIN		60
#define SSH2_MSG_USERAUTH_PER_METHOD_MAX		SSH2_MSG_USERAUTH_MAX
#define SSH2_MSG_CONNECTION_MIN				80
#define SSH2_MSG_CONNECTION_MAX				127
#define SSH2_MSG_RESERVED_MIN				128
#define SSH2_MSG_RESERVED_MAX				191
#define SSH2_MSG_LOCAL_MIN				192
#define SSH2_MSG_LOCAL_MAX				255
#define SSH2_MSG_MIN					1
#define SSH2_MSG_MAX					255

/* transport layer: generic */

#define SSH2_MSG_DISCONNECT				1
#define SSH2_MSG_IGNORE					2
#define SSH2_MSG_UNIMPLEMENTED				3
#define SSH2_MSG_DEBUG					4
#define SSH2_MSG_SERVICE_REQUEST			5
#define SSH2_MSG_SERVICE_ACCEPT				6
#define SSH2_MSG_EXT_INFO				7

/* transport layer: alg negotiation */

#define SSH2_MSG_KEXINIT				20
#define SSH2_MSG_NEWKEYS				21

/* transport layer: kex specific messages, can be reused */

#define SSH2_MSG_KEXDH_INIT				30
#define SSH2_MSG_KEXDH_REPLY				31

/* dh-group-exchange */
#define SSH2_MSG_KEX_DH_GEX_REQUEST_OLD			30
#define SSH2_MSG_KEX_DH_GEX_GROUP			31
#define SSH2_MSG_KEX_DH_GEX_INIT			32
#define SSH2_MSG_KEX_DH_GEX_REPLY			33
#define SSH2_MSG_KEX_DH_GEX_REQUEST			34

/* ecdh */
#define SSH2_MSG_KEX_ECDH_INIT				30
#define SSH2_MSG_KEX_ECDH_REPLY				31

/* user authentication: generic */

#define SSH2_MSG_USERAUTH_REQUEST			50
#define SSH2_MSG_USERAUTH_FAILURE			51
#define SSH2_MSG_USERAUTH_SUCCESS			52
#define SSH2_MSG_USERAUTH_BANNER			53

/* user authentication: method specific, can be reused */

#define SSH2_MSG_USERAUTH_PK_OK				60
#define SSH2_MSG_USERAUTH_PASSWD_CHANGEREQ		60
#define SSH2_MSG_USERAUTH_INFO_REQUEST			60
#define SSH2_MSG_USERAUTH_INFO_RESPONSE			61

/* connection protocol: generic */

#define SSH2_MSG_GLOBAL_REQUEST				80
#define SSH2_MSG_REQUEST_SUCCESS			81
#define SSH2_MSG_REQUEST_FAILURE			82

/* channel related messages */

#define SSH2_MSG_CHANNEL_OPEN				90
#define SSH2_MSG_CHANNEL_OPEN_CONFIRMATION		91
#define SSH2_MSG_CHANNEL_OPEN_FAILURE			92
#define SSH2_MSG_CHANNEL_WINDOW_ADJUST			93
#define SSH2_MSG_CHANNEL_DATA				94
#define SSH2_MSG_CHANNEL_EXTENDED_DATA			95
#define SSH2_MSG_CHANNEL_EOF				96
#define SSH2_MSG_CHANNEL_CLOSE				97
#define SSH2_MSG_CHANNEL_REQUEST			98
#define SSH2_MSG_CHANNEL_SUCCESS			99
#define SSH2_MSG_CHANNEL_FAILURE			100

/* disconnect reason code */

#define SSH2_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT	1
#define SSH2_DISCONNECT_PROTOCOL_ERROR			2
#define SSH2_DISCONNECT_KEY_EXCHANGE_FAILED		3
#define SSH2_DISCONNECT_HOST_AUTHENTICATION_FAILED	4
#define SSH2_DISCONNECT_RESERVED			4
#define SSH2_DISCONNECT_MAC_ERROR			5
#define SSH2_DISCONNECT_COMPRESSION_ERROR		6
#define SSH2_DISCONNECT_SERVICE_NOT_AVAILABLE		7
#define SSH2_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED	8
#define SSH2_DISCONNECT_HOST_KEY_NOT_VERIFIABLE		9
#define SSH2_DISCONNECT_CONNECTION_LOST			10
#define SSH2_DISCONNECT_BY_APPLICATION			11
#define SSH2_DISCONNECT_TOO_MANY_CONNECTIONS		12
#define SSH2_DISCONNECT_AUTH_CANCELLED_BY_USER		13
#define SSH2_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE	14
#define SSH2_DISCONNECT_ILLEGAL_USER_NAME		15

/* misc */

#define SSH2_OPEN_ADMINISTRATIVELY_PROHIBITED		1
#define SSH2_OPEN_CONNECT_FAILED			2
#define SSH2_OPEN_UNKNOWN_CHANNEL_TYPE			3
#define SSH2_OPEN_RESOURCE_SHORTAGE			4

#define SSH2_EXTENDED_DATA_STDERR			1

/* Certificate types for OpenSSH certificate keys extension */
#define SSH2_CERT_TYPE_USER				1
#define SSH2_CERT_TYPE_HOST				2
