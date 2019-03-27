/*
 * Copyright (c) 2002 - 2003
 * NetGroup, Politecnico di Torino (Italy)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * \file sockutils.c
 *
 * The goal of this file is to provide a common set of primitives for socket
 * manipulation.
 *
 * Although the socket interface defined in the RFC 2553 (and its updates)
 * is excellent, there are still differences between the behavior of those
 * routines on UN*X and Windows, and between UN*Xes.
 *
 * These calls provide an interface similar to the socket interface, but
 * that hides the differences between operating systems.  It does not
 * attempt to significantly improve on the socket interface in other
 * ways.
 */

#include "ftmacros.h"

#include <string.h>
#include <errno.h>	/* for the errno variable */
#include <stdio.h>	/* for the stderr file */
#include <stdlib.h>	/* for malloc() and free() */
#ifdef HAVE_LIMITS_H
#include <limits.h>
#else
#define INT_MAX		2147483647
#endif

#include "pcap-int.h"

#include "sockutils.h"
#include "portability.h"

#ifdef _WIN32
  /*
   * Winsock initialization.
   *
   * Ask for WinSock 2.2.
   */
  #define WINSOCK_MAJOR_VERSION 2
  #define WINSOCK_MINOR_VERSION 2

  static int sockcount = 0;	/*!< Variable that allows calling the WSAStartup() only one time */
#endif

/* Some minor differences between UNIX and Win32 */
#ifdef _WIN32
  #define SHUT_WR SD_SEND	/* The control code for shutdown() is different in Win32 */
#endif

/* Size of the buffer that has to keep error messages */
#define SOCK_ERRBUF_SIZE 1024

/* Constants; used in order to keep strings here */
#define SOCKET_NO_NAME_AVAILABLE "No name available"
#define SOCKET_NO_PORT_AVAILABLE "No port available"
#define SOCKET_NAME_NULL_DAD "Null address (possibly DAD Phase)"

/*
 * On UN*X, send() and recv() return ssize_t.
 *
 * On Windows, send() and recv() return an int.
 *
 *   Wth MSVC, there *is* no ssize_t.
 *
 *   With MinGW, there is an ssize_t type; it is either an int (32 bit)
 *   or a long long (64 bit). 
 *
 * So, on Windows, if we don't have ssize_t defined, define it as an
 * int, so we can use it, on all platforms, as the type of variables
 * that hold the return values from send() and recv().
 */
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
typedef int ssize_t;
#endif

/****************************************************
 *                                                  *
 * Locally defined functions                        *
 *                                                  *
 ****************************************************/

static int sock_ismcastaddr(const struct sockaddr *saddr);

/****************************************************
 *                                                  *
 * Function bodies                                  *
 *                                                  *
 ****************************************************/

/*
 * Format an error message given an errno value (UN*X) or a WinSock error
 * (Windows).
 */
void sock_fmterror(const char *caller, int errcode, char *errbuf, int errbuflen)
{
#ifdef _WIN32
	int retval;
	TCHAR message[SOCK_ERRBUF_SIZE];	/* It will be char (if we're using ascii) or wchar_t (if we're using unicode) */

	if (errbuf == NULL)
		return;

	retval = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
		FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL, errcode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		message, sizeof(message) / sizeof(TCHAR), NULL);

	if (retval == 0)
	{
		if ((caller) && (*caller))
			pcap_snprintf(errbuf, errbuflen, "%sUnable to get the exact error message", caller);
		else
			pcap_snprintf(errbuf, errbuflen, "Unable to get the exact error message");
	}
	else
	{
		if ((caller) && (*caller))
			pcap_snprintf(errbuf, errbuflen, "%s%s (code %d)", caller, message, errcode);
		else
			pcap_snprintf(errbuf, errbuflen, "%s (code %d)", message, errcode);
	}
#else
	char *message;

	if (errbuf == NULL)
		return;

	message = strerror(errcode);

	if ((caller) && (*caller))
		pcap_snprintf(errbuf, errbuflen, "%s%s (code %d)", caller, message, errcode);
	else
		pcap_snprintf(errbuf, errbuflen, "%s (code %d)", message, errcode);
#endif
}

/*
 * \brief It retrieves the error message after an error occurred in the socket interface.
 *
 * This function is defined because of the different way errors are returned in UNIX
 * and Win32. This function provides a consistent way to retrieve the error message
 * (after a socket error occurred) on all the platforms.
 *
 * \param caller: a pointer to a user-allocated string which contains a message that has
 * to be printed *before* the true error message. It could be, for example, 'this error
 * comes from the recv() call at line 31'. It may be NULL.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return No return values. The error message is returned in the 'string' parameter.
 */
void sock_geterror(const char *caller, char *errbuf, int errbuflen)
{
#ifdef _WIN32
	if (errbuf == NULL)
		return;
	sock_fmterror(caller, GetLastError(), errbuf, errbuflen);
#else
	if (errbuf == NULL)
		return;
	sock_fmterror(caller, errno, errbuf, errbuflen);
#endif
}

/*
 * \brief It initializes sockets.
 *
 * This function is pretty useless on UNIX, since socket initialization is not required.
 * However it is required on Win32. In UNIX, this function appears to be completely empty.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return '0' if everything is fine, '-1' if some errors occurred. The error message is returned
 * in the 'errbuf' variable.
 */
#ifdef _WIN32
int sock_init(char *errbuf, int errbuflen)
{
	if (sockcount == 0)
	{
		WSADATA wsaData;			/* helper variable needed to initialize Winsock */

		if (WSAStartup(MAKEWORD(WINSOCK_MAJOR_VERSION,
		    WINSOCK_MINOR_VERSION), &wsaData) != 0)
		{
			if (errbuf)
				pcap_snprintf(errbuf, errbuflen, "Failed to initialize Winsock\n");

			WSACleanup();

			return -1;
		}
	}

	sockcount++;
#else
int sock_init(char *errbuf _U_, int errbuflen _U_)
{
#endif
	return 0;
}

/*
 * \brief It deallocates sockets.
 *
 * This function is pretty useless on UNIX, since socket deallocation is not required.
 * However it is required on Win32. In UNIX, this function appears to be completely empty.
 *
 * \return No error values.
 */
void sock_cleanup(void)
{
#ifdef _WIN32
	sockcount--;

	if (sockcount == 0)
		WSACleanup();
#endif
}

/*
 * \brief It checks if the sockaddr variable contains a multicast address.
 *
 * \return '0' if the address is multicast, '-1' if it is not.
 */
static int sock_ismcastaddr(const struct sockaddr *saddr)
{
	if (saddr->sa_family == PF_INET)
	{
		struct sockaddr_in *saddr4 = (struct sockaddr_in *) saddr;
		if (IN_MULTICAST(ntohl(saddr4->sin_addr.s_addr))) return 0;
		else return -1;
	}
	else
	{
		struct sockaddr_in6 *saddr6 = (struct sockaddr_in6 *) saddr;
		if (IN6_IS_ADDR_MULTICAST(&saddr6->sin6_addr)) return 0;
		else return -1;
	}
}

/*
 * \brief It initializes a network connection both from the client and the server side.
 *
 * In case of a client socket, this function calls socket() and connect().
 * In the meanwhile, it checks for any socket error.
 * If an error occurs, it writes the error message into 'errbuf'.
 *
 * In case of a server socket, the function calls socket(), bind() and listen().
 *
 * This function is usually preceeded by the sock_initaddress().
 *
 * \param addrinfo: pointer to an addrinfo variable which will be used to
 * open the socket and such. This variable is the one returned by the previous call to
 * sock_initaddress().
 *
 * \param server: '1' if this is a server socket, '0' otherwise.
 *
 * \param nconn: number of the connections that are allowed to wait into the listen() call.
 * This value has no meanings in case of a client socket.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return the socket that has been opened (that has to be used in the following sockets calls)
 * if everything is fine, INVALID_SOCKET if some errors occurred. The error message is returned
 * in the 'errbuf' variable.
 */
SOCKET sock_open(struct addrinfo *addrinfo, int server, int nconn, char *errbuf, int errbuflen)
{
	SOCKET sock;
#if defined(SO_NOSIGPIPE) || defined(IPV6_V6ONLY) || defined(IPV6_BINDV6ONLY)
	int on = 1;
#endif

	sock = socket(addrinfo->ai_family, addrinfo->ai_socktype, addrinfo->ai_protocol);
	if (sock == INVALID_SOCKET)
	{
		sock_geterror("socket(): ", errbuf, errbuflen);
		return INVALID_SOCKET;
	}

	/*
	 * Disable SIGPIPE, if we have SO_NOSIGPIPE.  We don't want to
	 * have to deal with signals if the peer closes the connection,
	 * especially in client programs, which may not even be aware that
	 * they're sending to sockets.
	 */
#ifdef SO_NOSIGPIPE
	if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (char *)&on,
	    sizeof (int)) == -1)
	{
		sock_geterror("setsockopt(SO_NOSIGPIPE)", errbuf, errbuflen);
		closesocket(sock);
		return INVALID_SOCKET;
	}
#endif

	/* This is a server socket */
	if (server)
	{
#if defined(IPV6_V6ONLY) || defined(IPV6_BINDV6ONLY)
		/*
		 * Force the use of IPv6-only addresses.
		 *
		 * RFC 3493 indicates that you can support IPv4 on an
		 * IPv6 socket:
		 *
		 *    https://tools.ietf.org/html/rfc3493#section-3.7
		 *
		 * and that this is the default behavior.  This means
		 * that if we first create an IPv6 socket bound to the
		 * "any" address, it is, in effect, also bound to the
		 * IPv4 "any" address, so when we create an IPv4 socket
		 * and try to bind it to the IPv4 "any" address, it gets
		 * EADDRINUSE.
		 *
		 * Not all network stacks support IPv4 on IPv6 sockets;
		 * pre-NT 6 Windows stacks don't support it, and the
		 * OpenBSD stack doesn't support it for security reasons
		 * (see the OpenBSD inet6(4) man page).  Therefore, we
		 * don't want to rely on this behavior.
		 *
		 * So we try to disable it, using either the IPV6_V6ONLY
		 * option from RFC 3493:
		 *
		 *    https://tools.ietf.org/html/rfc3493#section-5.3
		 *
		 * or the IPV6_BINDV6ONLY option from older UN*Xes.
		 */
#ifndef IPV6_V6ONLY
  /* For older systems */
  #define IPV6_V6ONLY IPV6_BINDV6ONLY
#endif /* IPV6_V6ONLY */
		if (addrinfo->ai_family == PF_INET6)
		{
			if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
			    (char *)&on, sizeof (int)) == -1)
			{
				if (errbuf)
					pcap_snprintf(errbuf, errbuflen, "setsockopt(IPV6_V6ONLY)");
				closesocket(sock);
				return INVALID_SOCKET;
			}
		}
#endif /* defined(IPV6_V6ONLY) || defined(IPV6_BINDV6ONLY) */

		/* WARNING: if the address is a mcast one, I should place the proper Win32 code here */
		if (bind(sock, addrinfo->ai_addr, (int) addrinfo->ai_addrlen) != 0)
		{
			sock_geterror("bind(): ", errbuf, errbuflen);
			closesocket(sock);
			return INVALID_SOCKET;
		}

		if (addrinfo->ai_socktype == SOCK_STREAM)
			if (listen(sock, nconn) == -1)
			{
				sock_geterror("listen(): ", errbuf, errbuflen);
				closesocket(sock);
				return INVALID_SOCKET;
			}

		/* server side ended */
		return sock;
	}
	else	/* we're the client */
	{
		struct addrinfo *tempaddrinfo;
		char *errbufptr;
		size_t bufspaceleft;

		tempaddrinfo = addrinfo;
		errbufptr = errbuf;
		bufspaceleft = errbuflen;
		*errbufptr = 0;

		/*
		 * We have to loop though all the addinfo returned.
		 * For instance, we can have both IPv6 and IPv4 addresses, but the service we're trying
		 * to connect to is unavailable in IPv6, so we have to try in IPv4 as well
		 */
		while (tempaddrinfo)
		{

			if (connect(sock, tempaddrinfo->ai_addr, (int) tempaddrinfo->ai_addrlen) == -1)
			{
				size_t msglen;
				char TmpBuffer[100];
				char SocketErrorMessage[SOCK_ERRBUF_SIZE];

				/*
				 * We have to retrieve the error message before any other socket call completes, otherwise
				 * the error message is lost
				 */
				sock_geterror(NULL, SocketErrorMessage, sizeof(SocketErrorMessage));

				/* Returns the numeric address of the host that triggered the error */
				sock_getascii_addrport((struct sockaddr_storage *) tempaddrinfo->ai_addr, TmpBuffer, sizeof(TmpBuffer), NULL, 0, NI_NUMERICHOST, TmpBuffer, sizeof(TmpBuffer));

				pcap_snprintf(errbufptr, bufspaceleft,
				    "Is the server properly installed on %s?  connect() failed: %s", TmpBuffer, SocketErrorMessage);

				/* In case more then one 'connect' fails, we manage to keep all the error messages */
				msglen = strlen(errbufptr);

				errbufptr[msglen] = ' ';
				errbufptr[msglen + 1] = 0;

				bufspaceleft = bufspaceleft - (msglen + 1);
				errbufptr += (msglen + 1);

				tempaddrinfo = tempaddrinfo->ai_next;
			}
			else
				break;
		}

		/*
		 * Check how we exit from the previous loop
		 * If tempaddrinfo is equal to NULL, it means that all the connect() failed.
		 */
		if (tempaddrinfo == NULL)
		{
			closesocket(sock);
			return INVALID_SOCKET;
		}
		else
			return sock;
	}
}

/*
 * \brief Closes the present (TCP and UDP) socket connection.
 *
 * This function sends a shutdown() on the socket in order to disable send() calls
 * (while recv() ones are still allowed). Then, it closes the socket.
 *
 * \param sock: the socket identifier of the connection that has to be closed.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return '0' if everything is fine, '-1' if some errors occurred. The error message is returned
 * in the 'errbuf' variable.
 */
int sock_close(SOCKET sock, char *errbuf, int errbuflen)
{
	/*
	 * SHUT_WR: subsequent calls to the send function are disallowed.
	 * For TCP sockets, a FIN will be sent after all data is sent and
	 * acknowledged by the Server.
	 */
	if (shutdown(sock, SHUT_WR))
	{
		sock_geterror("shutdown(): ", errbuf, errbuflen);
		/* close the socket anyway */
		closesocket(sock);
		return -1;
	}

	closesocket(sock);
	return 0;
}

/*
 * \brief Checks that the address, port and flags given are valids and it returns an 'addrinfo' structure.
 *
 * This function basically calls the getaddrinfo() calls, and it performs a set of sanity checks
 * to control that everything is fine (e.g. a TCP socket cannot have a mcast address, and such).
 * If an error occurs, it writes the error message into 'errbuf'.
 *
 * \param host: a pointer to a string identifying the host. It can be
 * a host name, a numeric literal address, or NULL or "" (useful
 * in case of a server socket which has to bind to all addresses).
 *
 * \param port: a pointer to a user-allocated buffer containing the network port to use.
 *
 * \param hints: an addrinfo variable (passed by reference) containing the flags needed to create the
 * addrinfo structure appropriately.
 *
 * \param addrinfo: it represents the true returning value. This is a pointer to an addrinfo variable
 * (passed by reference), which will be allocated by this function and returned back to the caller.
 * This variable will be used in the next sockets calls.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return '0' if everything is fine, '-1' if some errors occurred. The error message is returned
 * in the 'errbuf' variable. The addrinfo variable that has to be used in the following sockets calls is
 * returned into the addrinfo parameter.
 *
 * \warning The 'addrinfo' variable has to be deleted by the programmer by calling freeaddrinfo() when
 * it is no longer needed.
 *
 * \warning This function requires the 'hints' variable as parameter. The semantic of this variable is the same
 * of the one of the corresponding variable used into the standard getaddrinfo() socket function. We suggest
 * the programmer to look at that function in order to set the 'hints' variable appropriately.
 */
int sock_initaddress(const char *host, const char *port,
    struct addrinfo *hints, struct addrinfo **addrinfo, char *errbuf, int errbuflen)
{
	int retval;

	retval = getaddrinfo(host, port, hints, addrinfo);
	if (retval != 0)
	{
		/*
		 * if the getaddrinfo() fails, you have to use gai_strerror(), instead of using the standard
		 * error routines (errno) in UNIX; Winsock suggests using the GetLastError() instead.
		 */
		if (errbuf)
		{
#ifdef _WIN32
			sock_geterror("getaddrinfo(): ", errbuf, errbuflen);
#else
			pcap_snprintf(errbuf, errbuflen, "getaddrinfo() %s", gai_strerror(retval));
#endif
		}
		return -1;
	}
	/*
	 * \warning SOCKET: I should check all the accept() in order to bind to all addresses in case
	 * addrinfo has more han one pointers
	 */

	/*
	 * This software only supports PF_INET and PF_INET6.
	 *
	 * XXX - should we just check that at least *one* address is
	 * either PF_INET or PF_INET6, and, when using the list,
	 * ignore all addresses that are neither?  (What, no IPX
	 * support? :-))
	 */
	if (((*addrinfo)->ai_family != PF_INET) &&
	    ((*addrinfo)->ai_family != PF_INET6))
	{
		if (errbuf)
			pcap_snprintf(errbuf, errbuflen, "getaddrinfo(): socket type not supported");
		freeaddrinfo(*addrinfo);
		*addrinfo = NULL;
		return -1;
	}

	/*
	 * You can't do multicast (or broadcast) TCP.
	 */
	if (((*addrinfo)->ai_socktype == SOCK_STREAM) &&
	    (sock_ismcastaddr((*addrinfo)->ai_addr) == 0))
	{
		if (errbuf)
			pcap_snprintf(errbuf, errbuflen, "getaddrinfo(): multicast addresses are not valid when using TCP streams");
		freeaddrinfo(*addrinfo);
		*addrinfo = NULL;
		return -1;
	}

	return 0;
}

/*
 * \brief It sends the amount of data contained into 'buffer' on the given socket.
 *
 * This function basically calls the send() socket function and it checks that all
 * the data specified in 'buffer' (of size 'size') will be sent. If an error occurs,
 * it writes the error message into 'errbuf'.
 * In case the socket buffer does not have enough space, it loops until all data
 * has been sent.
 *
 * \param socket: the connected socket currently opened.
 *
 * \param buffer: a char pointer to a user-allocated buffer in which data is contained.
 *
 * \param size: number of bytes that have to be sent.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return '0' if everything is fine, '-1' if an error other than
 * "connection reset" or "peer has closed the receive side" occurred,
 * '-2' if we got one of those errors.
 * For errors, an error message is returned in the 'errbuf' variable.
 */
int sock_send(SOCKET sock, const char *buffer, size_t size,
    char *errbuf, int errbuflen)
{
	int remaining;
	ssize_t nsent;

	if (size > INT_MAX)
	{
		if (errbuf)
		{
			pcap_snprintf(errbuf, errbuflen,
			    "Can't send more than %u bytes with sock_recv",
			    INT_MAX);
		}
		return -1;
	}
	remaining = (int)size;

	do {
#ifdef MSG_NOSIGNAL
		/*
		 * Send with MSG_NOSIGNAL, so that we don't get SIGPIPE
		 * on errors on stream-oriented sockets when the other
		 * end breaks the connection.
		 * The EPIPE error is still returned.
		 */
		nsent = send(sock, buffer, remaining, MSG_NOSIGNAL);
#else
		nsent = send(sock, buffer, remaining, 0);
#endif

		if (nsent == -1)
		{
			/*
			 * If the client closed the connection out from
			 * under us, there's no need to log that as an
			 * error.
			 */
			int errcode;

#ifdef _WIN32
			errcode = GetLastError();
			if (errcode == WSAECONNRESET ||
			    errcode == WSAECONNABORTED)
			{
				/*
				 * WSAECONNABORTED appears to be the error
				 * returned in Winsock when you try to send
				 * on a connection where the peer has closed
				 * the receive side.
				 */
				return -2;
			}
			sock_fmterror("send(): ", errcode, errbuf, errbuflen);
#else
			errcode = errno;
			if (errcode == ECONNRESET || errcode == EPIPE)
			{
				/*
				 * EPIPE is what's returned on UN*X when
				 * you try to send on a connection when
				 * the peer has closed the receive side.
				 */
				return -2;
			}
			sock_fmterror("send(): ", errcode, errbuf, errbuflen);
#endif
			return -1;
		}

		remaining -= nsent;
		buffer += nsent;
	} while (remaining != 0);

	return 0;
}

/*
 * \brief It copies the amount of data contained into 'buffer' into 'tempbuf'.
 * and it checks for buffer overflows.
 *
 * This function basically copies 'size' bytes of data contained into 'buffer'
 * into 'tempbuf', starting at offset 'offset'. Before that, it checks that the
 * resulting buffer will not be larger	than 'totsize'. Finally, it updates
 * the 'offset' variable in order to point to the first empty location of the buffer.
 *
 * In case the function is called with 'checkonly' equal to 1, it does not copy
 * the data into the buffer. It only checks for buffer overflows and it updates the
 * 'offset' variable. This mode can be useful when the buffer already contains the
 * data (maybe because the producer writes directly into the target buffer), so
 * only the buffer overflow check has to be made.
 * In this case, both 'buffer' and 'tempbuf' can be NULL values.
 *
 * This function is useful in case the userland application does not know immediately
 * all the data it has to write into the socket. This function provides a way to create
 * the "stream" step by step, appending the new data to the old one. Then, when all the
 * data has been bufferized, the application can call the sock_send() function.
 *
 * \param buffer: a char pointer to a user-allocated buffer that keeps the data
 * that has to be copied.
 *
 * \param size: number of bytes that have to be copied.
 *
 * \param tempbuf: user-allocated buffer (of size 'totsize') in which data
 * has to be copied.
 *
 * \param offset: an index into 'tempbuf' which keeps the location of its first
 * empty location.
 *
 * \param totsize: total size of the buffer in which data is being copied.
 *
 * \param checkonly: '1' if we do not want to copy data into the buffer and we
 * want just do a buffer ovreflow control, '0' if data has to be copied as well.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return '0' if everything is fine, '-1' if some errors occurred. The error message
 * is returned in the 'errbuf' variable. When the function returns, 'tempbuf' will
 * have the new string appended, and 'offset' will keep the length of that buffer.
 * In case of 'checkonly == 1', data is not copied, but 'offset' is updated in any case.
 *
 * \warning This function assumes that the buffer in which data has to be stored is
 * large 'totbuf' bytes.
 *
 * \warning In case of 'checkonly', be carefully to call this function *before* copying
 * the data into the buffer. Otherwise, the control about the buffer overflow is useless.
 */
int sock_bufferize(const char *buffer, int size, char *tempbuf, int *offset, int totsize, int checkonly, char *errbuf, int errbuflen)
{
	if ((*offset + size) > totsize)
	{
		if (errbuf)
			pcap_snprintf(errbuf, errbuflen, "Not enough space in the temporary send buffer.");
		return -1;
	}

	if (!checkonly)
		memcpy(tempbuf + (*offset), buffer, size);

	(*offset) += size;

	return 0;
}

/*
 * \brief It waits on a connected socket and it manages to receive data.
 *
 * This function basically calls the recv() socket function and it checks that no
 * error occurred. If that happens, it writes the error message into 'errbuf'.
 *
 * This function changes its behavior according to the 'receiveall' flag: if we
 * want to receive exactly 'size' byte, it loops on the recv()	until all the requested
 * data is arrived. Otherwise, it returns the data currently available.
 *
 * In case the socket does not have enough data available, it cycles on the recv()
 * until the requested data (of size 'size') is arrived.
 * In this case, it blocks until the number of bytes read is equal to 'size'.
 *
 * \param sock: the connected socket currently opened.
 *
 * \param buffer: a char pointer to a user-allocated buffer in which data has to be stored
 *
 * \param size: size of the allocated buffer. WARNING: this indicates the number of bytes
 * that we are expecting to be read.
 *
 * \param flags:
 *
 *   SOCK_RECEIVALL_XXX:
 *
 * 	if SOCK_RECEIVEALL_NO, return as soon as some data is ready
 *	if SOCK_RECEIVALL_YES, wait until 'size' data has been
 *	    received (in case the socket does not have enough data available).
 *
 *   SOCK_EOF_XXX:
 *
 *	if SOCK_EOF_ISNT_ERROR, if the first read returns 0, just return 0,
 *	    and return an error on any subsequent read that returns 0;
 *	if SOCK_EOF_IS_ERROR, if any read returns 0, return an error.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return the number of bytes read if everything is fine, '-1' if some errors occurred.
 * The error message is returned in the 'errbuf' variable.
 */

int sock_recv(SOCKET sock, void *buffer, size_t size, int flags,
    char *errbuf, int errbuflen)
{
	char *bufp = buffer;
	int remaining;
	ssize_t nread;

	if (size == 0)
	{
		SOCK_DEBUG_MESSAGE("I have been requested to read zero bytes");
		return 0;
	}
	if (size > INT_MAX)
	{
		if (errbuf)
		{
			pcap_snprintf(errbuf, errbuflen,
			    "Can't read more than %u bytes with sock_recv",
			    INT_MAX);
		}
		return -1;
	}

	bufp = (char *) buffer;
	remaining = (int) size;

	/*
	 * We don't use MSG_WAITALL because it's not supported in
	 * Win32.
	 */
	for (;;) {
		nread = recv(sock, bufp, remaining, 0);

		if (nread == -1)
		{
#ifndef _WIN32
			if (errno == EINTR)
				return -3;
#endif
			sock_geterror("recv(): ", errbuf, errbuflen);
			return -1;
		}

		if (nread == 0)
		{
			if ((flags & SOCK_EOF_IS_ERROR) ||
			    (remaining != (int) size))
			{
				/*
				 * Either we've already read some data,
				 * or we're always supposed to return
				 * an error on EOF.
				 */
				if (errbuf)
				{
					pcap_snprintf(errbuf, errbuflen,
					    "The other host terminated the connection.");
				}
				return -1;
			}
			else
				return 0;
		}

		/*
		 * Do we want to read the amount requested, or just return
		 * what we got?
		 */
		if (!(flags & SOCK_RECEIVEALL_YES))
		{
			/*
			 * Just return what we got.
			 */
			return (int) nread;
		}

		bufp += nread;
		remaining -= nread;

		if (remaining == 0)
			return (int) size;
	}
}

/*
 * Receives a datagram from a socket.
 *
 * Returns the size of the datagram on success or -1 on error.
 */
int sock_recv_dgram(SOCKET sock, void *buffer, size_t size,
    char *errbuf, int errbuflen)
{
	ssize_t nread;
#ifndef _WIN32
	struct msghdr message;
	struct iovec iov;
#endif

	if (size == 0)
	{
		SOCK_DEBUG_MESSAGE("I have been requested to read zero bytes");
		return 0;
	}
	if (size > INT_MAX)
	{
		if (errbuf)
		{
			pcap_snprintf(errbuf, errbuflen,
			    "Can't read more than %u bytes with sock_recv_dgram",
			    INT_MAX);
		}
		return -1;
	}

	/*
	 * This should be a datagram socket, so we should get the
	 * entire datagram in one recv() or recvmsg() call, and
	 * don't need to loop.
	 */
#ifdef _WIN32
	nread = recv(sock, buffer, size, 0);
	if (nread == SOCKET_ERROR)
	{
		/*
		 * To quote the MSDN documentation for recv(),
		 * "If the datagram or message is larger than
		 * the buffer specified, the buffer is filled
		 * with the first part of the datagram, and recv
		 * generates the error WSAEMSGSIZE. For unreliable
		 * protocols (for example, UDP) the excess data is
		 * lost..."
		 *
		 * So if the message is bigger than the buffer
		 * supplied to us, the excess data is discarded,
		 * and we'll report an error.
		 */
		sock_geterror("recv(): ", errbuf, errbuflen);
		return -1;
	}
#else /* _WIN32 */
	/*
	 * The Single UNIX Specification says that a recv() on
	 * a socket for a message-oriented protocol will discard
	 * the excess data.  It does *not* indicate that the
	 * receive will fail with, for example, EMSGSIZE.
	 *
	 * Therefore, we use recvmsg(), which appears to be
	 * the only way to get a "message truncated" indication
	 * when receiving a message for a message-oriented
	 * protocol.
	 */
	message.msg_name = NULL;	/* we don't care who it's from */
	message.msg_namelen = 0;
	iov.iov_base = buffer;
	iov.iov_len = size;
	message.msg_iov = &iov;
	message.msg_iovlen = 1;
#ifdef HAVE_STRUCT_MSGHDR_MSG_CONTROL
	message.msg_control = NULL;	/* we don't care about control information */
	message.msg_controllen = 0;
#endif
#ifdef HAVE_STRUCT_MSGHDR_MSG_FLAGS
	message.msg_flags = 0;
#endif
	nread = recvmsg(sock, &message, 0);
	if (nread == -1)
	{
		if (errno == EINTR)
			return -3;
		sock_geterror("recv(): ", errbuf, errbuflen);
		return -1;
	}
#ifdef HAVE_STRUCT_MSGHDR_MSG_FLAGS
	/*
	 * XXX - Solaris supports this, but only if you ask for the
	 * X/Open version of recvmsg(); should we use that, or will
	 * that cause other problems?
	 */
	if (message.msg_flags & MSG_TRUNC)
	{
		/*
		 * Message was bigger than the specified buffer size.
		 *
		 * Report this as an error, as the Microsoft documentation
		 * implies we'd do in a similar case on Windows.
		 */
		pcap_snprintf(errbuf, errbuflen, "recv(): Message too long");
		return -1;
	}
#endif /* HAVE_STRUCT_MSGHDR_MSG_FLAGS */
#endif /* _WIN32 */

	/*
	 * The size we're reading fits in an int, so the return value
	 * will fit in an int.
	 */
	return (int)nread;
}

/*
 * \brief It discards N bytes that are currently waiting to be read on the current socket.
 *
 * This function is useful in case we receive a message we cannot understand (e.g.
 * wrong version number when receiving a network packet), so that we have to discard all
 * data before reading a new message.
 *
 * This function will read 'size' bytes from the socket and discard them.
 * It defines an internal buffer in which data will be copied; however, in case
 * this buffer is not large enough, it will cycle in order to read everything as well.
 *
 * \param sock: the connected socket currently opened.
 *
 * \param size: number of bytes that have to be discarded.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return '0' if everything is fine, '-1' if some errors occurred.
 * The error message is returned in the 'errbuf' variable.
 */
int sock_discard(SOCKET sock, int size, char *errbuf, int errbuflen)
{
#define TEMP_BUF_SIZE 32768

	char buffer[TEMP_BUF_SIZE];		/* network buffer, to be used when the message is discarded */

	/*
	 * A static allocation avoids the need of a 'malloc()' each time we want to discard a message
	 * Our feeling is that a buffer if 32KB is enough for most of the application;
	 * in case this is not enough, the "while" loop discards the message by calling the
	 * sockrecv() several times.
	 * We do not want to create a bigger variable because this causes the program to exit on
	 * some platforms (e.g. BSD)
	 */
	while (size > TEMP_BUF_SIZE)
	{
		if (sock_recv(sock, buffer, TEMP_BUF_SIZE, SOCK_RECEIVEALL_YES, errbuf, errbuflen) == -1)
			return -1;

		size -= TEMP_BUF_SIZE;
	}

	/*
	 * If there is still data to be discarded
	 * In this case, the data can fit into the temporary buffer
	 */
	if (size)
	{
		if (sock_recv(sock, buffer, size, SOCK_RECEIVEALL_YES, errbuf, errbuflen) == -1)
			return -1;
	}

	SOCK_DEBUG_MESSAGE("I'm currently discarding data\n");

	return 0;
}

/*
 * \brief Checks that one host (identified by the sockaddr_storage structure) belongs to an 'allowed list'.
 *
 * This function is useful after an accept() call in order to check if the connecting
 * host is allowed to connect to me. To do that, we have a buffer that keeps the list of the
 * allowed host; this function checks the sockaddr_storage structure of the connecting host
 * against this host list, and it returns '0' is the host is included in this list.
 *
 * \param hostlist: pointer to a string that contains the list of the allowed host.
 *
 * \param sep: a string that keeps the separators used between the hosts (for example the
 * space character) in the host list.
 *
 * \param from: a sockaddr_storage structure, as it is returned by the accept() call.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return It returns:
 * - '1' if the host list is empty
 * - '0' if the host belongs to the host list (and therefore it is allowed to connect)
 * - '-1' in case the host does not belong to the host list (and therefore it is not allowed to connect
 * - '-2' in case or error. The error message is returned in the 'errbuf' variable.
 */
int sock_check_hostlist(char *hostlist, const char *sep, struct sockaddr_storage *from, char *errbuf, int errbuflen)
{
	/* checks if the connecting host is among the ones allowed */
	if ((hostlist) && (hostlist[0]))
	{
		char *token;					/* temp, needed to separate items into the hostlist */
		struct addrinfo *addrinfo, *ai_next;
		char *temphostlist;
		char *lasts;

		/*
		 * The problem is that strtok modifies the original variable by putting '0' at the end of each token
		 * So, we have to create a new temporary string in which the original content is kept
		 */
		temphostlist = strdup(hostlist);
		if (temphostlist == NULL)
		{
			sock_geterror("sock_check_hostlist(), malloc() failed", errbuf, errbuflen);
			return -2;
		}

		token = pcap_strtok_r(temphostlist, sep, &lasts);

		/* it avoids a warning in the compilation ('addrinfo used but not initialized') */
		addrinfo = NULL;

		while (token != NULL)
		{
			struct addrinfo hints;
			int retval;

			addrinfo = NULL;
			memset(&hints, 0, sizeof(struct addrinfo));
			hints.ai_family = PF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;

			retval = getaddrinfo(token, "0", &hints, &addrinfo);
			if (retval != 0)
			{
				if (errbuf)
					pcap_snprintf(errbuf, errbuflen, "getaddrinfo() %s", gai_strerror(retval));

				SOCK_DEBUG_MESSAGE(errbuf);

				/* Get next token */
				token = pcap_strtok_r(NULL, sep, &lasts);
				continue;
			}

			/* ai_next is required to preserve the content of addrinfo, in order to deallocate it properly */
			ai_next = addrinfo;
			while (ai_next)
			{
				if (sock_cmpaddr(from, (struct sockaddr_storage *) ai_next->ai_addr) == 0)
				{
					free(temphostlist);
					freeaddrinfo(addrinfo);
					return 0;
				}

				/*
				 * If we are here, it means that the current address does not matches
				 * Let's try with the next one in the header chain
				 */
				ai_next = ai_next->ai_next;
			}

			freeaddrinfo(addrinfo);
			addrinfo = NULL;

			/* Get next token */
			token = pcap_strtok_r(NULL, sep, &lasts);
		}

		if (addrinfo)
		{
			freeaddrinfo(addrinfo);
			addrinfo = NULL;
		}

		if (errbuf)
			pcap_snprintf(errbuf, errbuflen, "The host is not in the allowed host list. Connection refused.");

		free(temphostlist);
		return -1;
	}

	/* No hostlist, so we have to return 'empty list' */
	return 1;
}

/*
 * \brief Compares two addresses contained into two sockaddr_storage structures.
 *
 * This function is useful to compare two addresses, given their internal representation,
 * i.e. an sockaddr_storage structure.
 *
 * The two structures do not need to be sockaddr_storage; you can have both 'sockaddr_in' and
 * sockaddr_in6, properly acsted in order to be compliant to the function interface.
 *
 * This function will return '0' if the two addresses matches, '-1' if not.
 *
 * \param first: a sockaddr_storage structure, (for example the one that is returned by an
 * accept() call), containing the first address to compare.
 *
 * \param second: a sockaddr_storage structure containing the second address to compare.
 *
 * \return '0' if the addresses are equal, '-1' if they are different.
 */
int sock_cmpaddr(struct sockaddr_storage *first, struct sockaddr_storage *second)
{
	if (first->ss_family == second->ss_family)
	{
		if (first->ss_family == AF_INET)
		{
			if (memcmp(&(((struct sockaddr_in *) first)->sin_addr),
				&(((struct sockaddr_in *) second)->sin_addr),
				sizeof(struct in_addr)) == 0)
				return 0;
		}
		else /* address family is AF_INET6 */
		{
			if (memcmp(&(((struct sockaddr_in6 *) first)->sin6_addr),
				&(((struct sockaddr_in6 *) second)->sin6_addr),
				sizeof(struct in6_addr)) == 0)
				return 0;
		}
	}

	return -1;
}

/*
 * \brief It gets the address/port the system picked for this socket (on connected sockets).
 *
 * It is used to return the address and port the server picked for our socket on the local machine.
 * It works only on:
 * - connected sockets
 * - server sockets
 *
 * On unconnected client sockets it does not work because the system dynamically chooses a port
 * only when the socket calls a send() call.
 *
 * \param sock: the connected socket currently opened.
 *
 * \param address: it contains the address that will be returned by the function. This buffer
 * must be properly allocated by the user. The address can be either literal or numeric depending
 * on the value of 'Flags'.
 *
 * \param addrlen: the length of the 'address' buffer.
 *
 * \param port: it contains the port that will be returned by the function. This buffer
 * must be properly allocated by the user.
 *
 * \param portlen: the length of the 'port' buffer.
 *
 * \param flags: a set of flags (the ones defined into the getnameinfo() standard socket function)
 * that determine if the resulting address must be in numeric / literal form, and so on.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return It returns '-1' if this function succeeds, '0' otherwise.
 * The address and port corresponding are returned back in the buffers 'address' and 'port'.
 * In any case, the returned strings are '0' terminated.
 *
 * \warning If the socket is using a connectionless protocol, the address may not be available
 * until I/O occurs on the socket.
 */
int sock_getmyinfo(SOCKET sock, char *address, int addrlen, char *port, int portlen, int flags, char *errbuf, int errbuflen)
{
	struct sockaddr_storage mysockaddr;
	socklen_t sockaddrlen;


	sockaddrlen = sizeof(struct sockaddr_storage);

	if (getsockname(sock, (struct sockaddr *) &mysockaddr, &sockaddrlen) == -1)
	{
		sock_geterror("getsockname(): ", errbuf, errbuflen);
		return 0;
	}

	/* Returns the numeric address of the host that triggered the error */
	return sock_getascii_addrport(&mysockaddr, address, addrlen, port, portlen, flags, errbuf, errbuflen);
}

/*
 * \brief It retrieves two strings containing the address and the port of a given 'sockaddr' variable.
 *
 * This function is basically an extended version of the inet_ntop(), which does not exist in
 * Winsock because the same result can be obtained by using the getnameinfo().
 * However, differently from inet_ntop(), this function is able to return also literal names
 * (e.g. 'localhost') dependently from the 'Flags' parameter.
 *
 * The function accepts a sockaddr_storage variable (which can be returned by several functions
 * like bind(), connect(), accept(), and more) and it transforms its content into a 'human'
 * form. So, for instance, it is able to translate an hex address (stored in binary form) into
 * a standard IPv6 address like "::1".
 *
 * The behavior of this function depends on the parameters we have in the 'Flags' variable, which
 * are the ones allowed in the standard getnameinfo() socket function.
 *
 * \param sockaddr: a 'sockaddr_in' or 'sockaddr_in6' structure containing the address that
 * need to be translated from network form into the presentation form. This structure must be
 * zero-ed prior using it, and the address family field must be filled with the proper value.
 * The user must cast any 'sockaddr_in' or 'sockaddr_in6' structures to 'sockaddr_storage' before
 * calling this function.
 *
 * \param address: it contains the address that will be returned by the function. This buffer
 * must be properly allocated by the user. The address can be either literal or numeric depending
 * on the value of 'Flags'.
 *
 * \param addrlen: the length of the 'address' buffer.
 *
 * \param port: it contains the port that will be returned by the function. This buffer
 * must be properly allocated by the user.
 *
 * \param portlen: the length of the 'port' buffer.
 *
 * \param flags: a set of flags (the ones defined into the getnameinfo() standard socket function)
 * that determine if the resulting address must be in numeric / literal form, and so on.
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return It returns '-1' if this function succeeds, '0' otherwise.
 * The address and port corresponding to the given SockAddr are returned back in the buffers 'address'
 * and 'port'.
 * In any case, the returned strings are '0' terminated.
 */
int sock_getascii_addrport(const struct sockaddr_storage *sockaddr, char *address, int addrlen, char *port, int portlen, int flags, char *errbuf, int errbuflen)
{
	socklen_t sockaddrlen;
	int retval;					/* Variable that keeps the return value; */

	retval = -1;

#ifdef _WIN32
	if (sockaddr->ss_family == AF_INET)
		sockaddrlen = sizeof(struct sockaddr_in);
	else
		sockaddrlen = sizeof(struct sockaddr_in6);
#else
	sockaddrlen = sizeof(struct sockaddr_storage);
#endif

	if ((flags & NI_NUMERICHOST) == 0)	/* Check that we want literal names */
	{
		if ((sockaddr->ss_family == AF_INET6) &&
			(memcmp(&((struct sockaddr_in6 *) sockaddr)->sin6_addr, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", sizeof(struct in6_addr)) == 0))
		{
			if (address)
				strlcpy(address, SOCKET_NAME_NULL_DAD, addrlen);
			return retval;
		}
	}

	if (getnameinfo((struct sockaddr *) sockaddr, sockaddrlen, address, addrlen, port, portlen, flags) != 0)
	{
		/* If the user wants to receive an error message */
		if (errbuf)
		{
			sock_geterror("getnameinfo(): ", errbuf, errbuflen);
			errbuf[errbuflen - 1] = 0;
		}

		if (address)
		{
			strlcpy(address, SOCKET_NO_NAME_AVAILABLE, addrlen);
			address[addrlen - 1] = 0;
		}

		if (port)
		{
			strlcpy(port, SOCKET_NO_PORT_AVAILABLE, portlen);
			port[portlen - 1] = 0;
		}

		retval = 0;
	}

	return retval;
}

/*
 * \brief It translates an address from the 'presentation' form into the 'network' form.
 *
 * This function basically replaces inet_pton(), which does not exist in Winsock because
 * the same result can be obtained by using the getaddrinfo().
 * An additional advantage is that 'Address' can be both a numeric address (e.g. '127.0.0.1',
 * like in inet_pton() ) and a literal name (e.g. 'localhost').
 *
 * This function does the reverse job of sock_getascii_addrport().
 *
 * \param address: a zero-terminated string which contains the name you have to
 * translate. The name can be either literal (e.g. 'localhost') or numeric (e.g. '::1').
 *
 * \param sockaddr: a user-allocated sockaddr_storage structure which will contains the
 * 'network' form of the requested address.
 *
 * \param addr_family: a constant which can assume the following values:
 * - 'AF_INET' if we want to ping an IPv4 host
 * - 'AF_INET6' if we want to ping an IPv6 host
 * - 'AF_UNSPEC' if we do not have preferences about the protocol used to ping the host
 *
 * \param errbuf: a pointer to an user-allocated buffer that will contain the complete
 * error message. This buffer has to be at least 'errbuflen' in length.
 * It can be NULL; in this case the error cannot be printed.
 *
 * \param errbuflen: length of the buffer that will contains the error. The error message cannot be
 * larger than 'errbuflen - 1' because the last char is reserved for the string terminator.
 *
 * \return '-1' if the translation succeeded, '-2' if there was some non critical error, '0'
 * otherwise. In case it fails, the content of the SockAddr variable remains unchanged.
 * A 'non critical error' can occur in case the 'Address' is a literal name, which can be mapped
 * to several network addresses (e.g. 'foo.bar.com' => '10.2.2.2' and '10.2.2.3'). In this case
 * the content of the SockAddr parameter will be the address corresponding to the first mapping.
 *
 * \warning The sockaddr_storage structure MUST be allocated by the user.
 */
int sock_present2network(const char *address, struct sockaddr_storage *sockaddr, int addr_family, char *errbuf, int errbuflen)
{
	int retval;
	struct addrinfo *addrinfo;
	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = addr_family;

	if ((retval = sock_initaddress(address, "22222" /* fake port */, &hints, &addrinfo, errbuf, errbuflen)) == -1)
		return 0;

	if (addrinfo->ai_family == PF_INET)
		memcpy(sockaddr, addrinfo->ai_addr, sizeof(struct sockaddr_in));
	else
		memcpy(sockaddr, addrinfo->ai_addr, sizeof(struct sockaddr_in6));

	if (addrinfo->ai_next != NULL)
	{
		freeaddrinfo(addrinfo);

		if (errbuf)
			pcap_snprintf(errbuf, errbuflen, "More than one socket requested; using the first one returned");
		return -2;
	}

	freeaddrinfo(addrinfo);
	return -1;
}
