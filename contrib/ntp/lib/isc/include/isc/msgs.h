/*
 * Copyright (C) 2004-2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: msgs.h,v 1.19 2009/10/01 23:48:08 tbox Exp $ */

#ifndef ISC_MSGS_H
#define ISC_MSGS_H 1

/*! \file isc/msgs.h */

#include <isc/lib.h>		/* Provide isc_msgcat global variable. */
#include <isc/msgcat.h>		/* Provide isc_msgcat_*() functions. */

/*@{*/
/*!
 * \brief Message sets, named per source file, excepting "GENERAL".
 *
 * IMPORTANT: The original list is alphabetical, but any new sets must
 * be added to the end.
 */
#define ISC_MSGSET_GENERAL	1
/*	ISC_RESULT_RESULTSET    2 */     /* XXX */
/*	ISC_RESULT_UNAVAILABLESET 3 */   /* XXX */
#define ISC_MSGSET_APP		4
#define ISC_MSGSET_COMMANDLINE	5
#define ISC_MSGSET_ENTROPY	6
#define ISC_MSGSET_IFITERIOCTL	7
#define ISC_MSGSET_IFITERSYSCTL	8
#define ISC_MSGSET_LEX		9
#define ISC_MSGSET_LOG		10
#define ISC_MSGSET_MEM		11
#define ISC_MSGSET_NETADDR	12
#define ISC_MSGSET_PRINT	13
#define ISC_MSGSET_RESULT	14
#define ISC_MSGSET_RWLOCK	15
#define ISC_MSGSET_SOCKADDR	16
#define ISC_MSGSET_SOCKET	17
#define ISC_MSGSET_TASK		18
#define ISC_MSGSET_TIMER	19
#define ISC_MSGSET_UTIL		20
#define ISC_MSGSET_IFITERGETIFADDRS 21
/*@}*/

/*@{*/
/*!
 * Message numbers
 * are only required to be unique per message set,
 * but are unique throughout the entire catalog to not be as confusing when
 * debugging.
 *
 * The initial numbering was done by multiply by 100 the set number the
 * message appears in then adding the incremental message number.
 */
#define ISC_MSG_FAILED		101 /*%< "failed" */
#define ISC_MSG_SUCCEEDED	102 /*%< Compatible with "failed" */
#define ISC_MSG_SUCCESS		103 /*%< More usual way to say "success" */
#define ISC_MSG_STARTING	104 /*%< As in "daemon: starting" */
#define ISC_MSG_STOPING		105 /*%< As in "daemon: stopping" */
#define ISC_MSG_ENTERING	106 /*%< As in "some_subr: entering" */
#define ISC_MSG_EXITING		107 /*%< As in "some_subr: exiting" */
#define ISC_MSG_CALLING		108 /*%< As in "calling some_subr()" */
#define ISC_MSG_RETURNED	109 /*%< As in "some_subr: returned <foo>" */
#define ISC_MSG_FATALERROR	110 /*%< "fatal error" */
#define ISC_MSG_SHUTTINGDOWN	111 /*%< "shutting down" */
#define ISC_MSG_RUNNING		112 /*%< "running" */
#define ISC_MSG_WAIT		113 /*%< "wait" */
#define ISC_MSG_WAITUNTIL	114 /*%< "waituntil" */

#define ISC_MSG_SIGNALSETUP	201 /*%< "handle_signal() %d setup: %s" */

#define ISC_MSG_ILLEGALOPT	301 /*%< "illegal option" */
#define ISC_MSG_OPTNEEDARG	302 /*%< "option requires an argument" */

#define ISC_MSG_ENTROPYSTATS	401 /*%< "Entropy pool %p:  refcnt %u ..." */

#define ISC_MSG_MAKESCANSOCKET	501 /*%< "making interface scan socket: %s" */
#define ISC_MSG_GETIFCONFIG	502 /*%< "get interface configuration: %s" */
#define ISC_MSG_BUFFERMAX	503 /*%< "... maximum buffer size exceeded" */
#define ISC_MSG_GETDESTADDR	504 /*%< "%s: getting destination address: %s" */
#define ISC_MSG_GETNETMASK	505 /*%< "%s: getting netmask: %s" */
#define ISC_MSG_GETBCSTADDR	506 /*%< "%s: getting broadcast address: %s" */

#define ISC_MSG_GETIFLISTSIZE	601 /*%< "getting interface list size: ..." */
#define ISC_MSG_GETIFLIST	602 /*%< "getting interface list: ..." */
#define ISC_MSG_UNEXPECTEDTYPE	603 /*%< "... unexpected ... message type" */

#define ISC_MSG_UNEXPECTEDSTATE	701 /*%< "Unexpected state %d" */

#define ISC_MSG_BADTIME		801 /*%< "Bad 00 99:99:99.999 " */
#define ISC_MSG_LEVEL		802 /*%< "level %d: " */

#define ISC_MSG_ADDTRACE	901 /*%< "add %p size %u " */
#define ISC_MSG_DELTRACE	902 /*%< "del %p size %u " */
#define ISC_MSG_POOLSTATS	903 /*%< "[Pool statistics]\n" */
#define ISC_MSG_POOLNAME	904 /*%< "name" */
#define ISC_MSG_POOLSIZE	905 /*%< "size" */
#define ISC_MSG_POOLMAXALLOC	906 /*%< "maxalloc" */
#define ISC_MSG_POOLALLOCATED	907 /*%< "allocated" */
#define ISC_MSG_POOLFREECOUNT	908 /*%< "freecount" */
#define ISC_MSG_POOLFREEMAX	909 /*%< "freemax" */
#define ISC_MSG_POOLFILLCOUNT	910 /*%< "fillcount" */
#define ISC_MSG_POOLGETS	911 /*%< "gets" */
#define ISC_MSG_DUMPALLOC	912 /*%< "DUMP OF ALL OUTSTANDING MEMORY ..." */
#define ISC_MSG_NONE		913 /*%< "\tNone.\n" */
#define ISC_MSG_PTRFILELINE	914 /*%< "\tptr %p file %s line %u\n" */

#define ISC_MSG_UNKNOWNADDR    1001 /*%< "<unknown address, family %u>" */

#define ISC_MSG_NOLONGDBL      1104 /*%< "long doubles are not supported" */

#define ISC_MSG_PRINTLOCK      1201 /*%< "rwlock %p thread %lu ..." */
#define ISC_MSG_READ	       1202 /*%< "read" */
#define ISC_MSG_WRITE	       1203 /*%< "write" */
#define ISC_MSG_READING	       1204 /*%< "reading" */
#define ISC_MSG_WRITING	       1205 /*%< "writing" */
#define ISC_MSG_PRELOCK	       1206 /*%< "prelock" */
#define ISC_MSG_POSTLOCK       1207 /*%< "postlock" */
#define ISC_MSG_PREUNLOCK      1208 /*%< "preunlock" */
#define ISC_MSG_POSTUNLOCK     1209 /*%< "postunlock" */

#define ISC_MSG_UNKNOWNFAMILY  1301 /*%< "unknown address family: %d" */

#define ISC_MSG_WRITEFAILED    1401 /*%< "write() failed during watcher ..." */
#define ISC_MSG_READFAILED     1402 /*%< "read() failed during watcher ... " */
#define ISC_MSG_PROCESSCMSG    1403 /*%< "processing cmsg %p" */
#define ISC_MSG_IFRECEIVED     1404 /*%< "interface received on ifindex %u" */
#define ISC_MSG_SENDTODATA     1405 /*%< "sendto pktinfo data, ifindex %u" */
#define ISC_MSG_DOIORECV       1406 /*%< "doio_recv: recvmsg(%d) %d bytes ..." */
#define ISC_MSG_PKTRECV	       1407 /*%< "packet received correctly" */
#define ISC_MSG_DESTROYING     1408 /*%< "destroying" */
#define ISC_MSG_CREATED	       1409 /*%< "created" */
#define ISC_MSG_ACCEPTLOCK     1410 /*%< "internal_accept called, locked ..." */
#define ISC_MSG_ACCEPTEDCXN    1411 /*%< "accepted connection, new socket %p" */
#define ISC_MSG_INTERNALRECV   1412 /*%< "internal_recv: task %p got event %p" */
#define ISC_MSG_INTERNALSEND   1413 /*%< "internal_send: task %p got event %p" */
#define ISC_MSG_WATCHERMSG     1414 /*%< "watcher got message %d" */
#define ISC_MSG_SOCKETSREMAIN  1415 /*%< "sockets exist" */
#define ISC_MSG_PKTINFOPROVIDED	1416 /*%< "pktinfo structure provided, ..." */
#define ISC_MSG_BOUND	       1417 /*%< "bound" */
#define ISC_MSG_ACCEPTRETURNED 1418 /*%< accept() returned %d/%s */
#define ISC_MSG_TOOMANYFDS     1419 /*%< %s: too many open file descriptors */
#define ISC_MSG_ZEROPORT       1420 /*%< dropping source port zero packet */
#define ISC_MSG_FILTER	       1421 /*%< setsockopt(SO_ACCEPTFILTER): %s */

#define ISC_MSG_TOOMANYHANDLES 1422 /*%< %s: too many open WSA event handles: %s */
#define ISC_MSG_POKED          1423 /*%< "poked flags: %d" */

#define ISC_MSG_AWAKE	       1502 /*%< "awake" */
#define ISC_MSG_WORKING	       1503 /*%< "working" */
#define ISC_MSG_EXECUTE	       1504 /*%< "execute action" */
#define ISC_MSG_EMPTY	       1505 /*%< "empty" */
#define ISC_MSG_DONE	       1506 /*%< "done" */
#define ISC_MSG_QUANTUM	       1507 /*%< "quantum" */

#define ISC_MSG_SCHEDULE       1601 /*%< "schedule" */
#define ISC_MSG_SIGNALSCHED    1602 /*%< "signal (schedule)" */
#define ISC_MSG_SIGNALDESCHED  1603 /*%< "signal (deschedule)" */
#define ISC_MSG_SIGNALDESTROY  1604 /*%< "signal (destroy)" */
#define ISC_MSG_IDLERESCHED    1605 /*%< "idle reschedule" */
#define ISC_MSG_EVENTNOTALLOC  1606 /*%< "couldn't allocate event" */
#define ISC_MSG_SCHEDFAIL      1607 /*%< "couldn't schedule timer: %u" */
#define ISC_MSG_POSTING	       1608 /*%< "posting" */
#define ISC_MSG_WAKEUP	       1609 /*%< "wakeup" */

#define ISC_MSG_LOCK	       1701 /*%< "LOCK" */
#define ISC_MSG_LOCKING	       1702 /*%< "LOCKING" */
#define ISC_MSG_LOCKED	       1703 /*%< "LOCKED" */
#define ISC_MSG_UNLOCKED       1704 /*%< "UNLOCKED" */
#define ISC_MSG_RWLOCK	       1705 /*%< "RWLOCK" */
#define ISC_MSG_RWLOCKED       1706 /*%< "RWLOCKED" */
#define ISC_MSG_RWUNLOCK       1707 /*%< "RWUNLOCK" */
#define ISC_MSG_BROADCAST      1708 /*%< "BROADCAST" */
#define ISC_MSG_SIGNAL	       1709 /*%< "SIGNAL" */
#define ISC_MSG_UTILWAIT       1710 /*%< "WAIT" */
#define ISC_MSG_WAITED	       1711 /*%< "WAITED" */

#define ISC_MSG_GETIFADDRS     1801 /*%< "getting interface addresses: ..." */

/*@}*/

#endif /* ISC_MSGS_H */
