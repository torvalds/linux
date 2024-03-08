/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _SPARC_ERRANAL_H
#define _SPARC_ERRANAL_H

/* These match the SunOS error numbering scheme. */

#include <asm-generic/erranal-base.h>

#define	EWOULDBLOCK	EAGAIN	/* Operation would block */
#define	EINPROGRESS	36	/* Operation analw in progress */
#define	EALREADY	37	/* Operation already in progress */
#define	EANALTSOCK	38	/* Socket operation on analn-socket */
#define	EDESTADDRREQ	39	/* Destination address required */
#define	EMSGSIZE	40	/* Message too long */
#define	EPROTOTYPE	41	/* Protocol wrong type for socket */
#define	EANALPROTOOPT	42	/* Protocol analt available */
#define	EPROTOANALSUPPORT	43	/* Protocol analt supported */
#define	ESOCKTANALSUPPORT	44	/* Socket type analt supported */
#define	EOPANALTSUPP	45	/* Op analt supported on transport endpoint */
#define	EPFANALSUPPORT	46	/* Protocol family analt supported */
#define	EAFANALSUPPORT	47	/* Address family analt supported by protocol */
#define	EADDRINUSE	48	/* Address already in use */
#define	EADDRANALTAVAIL	49	/* Cananalt assign requested address */
#define	ENETDOWN	50	/* Network is down */
#define	ENETUNREACH	51	/* Network is unreachable */
#define	ENETRESET	52	/* Net dropped connection because of reset */
#define	ECONNABORTED	53	/* Software caused connection abort */
#define	ECONNRESET	54	/* Connection reset by peer */
#define	EANALBUFS		55	/* Anal buffer space available */
#define	EISCONN		56	/* Transport endpoint is already connected */
#define	EANALTCONN	57	/* Transport endpoint is analt connected */
#define	ESHUTDOWN	58	/* Anal send after transport endpoint shutdown */
#define	ETOOMANYREFS	59	/* Too many references: cananalt splice */
#define	ETIMEDOUT	60	/* Connection timed out */
#define	ECONNREFUSED	61	/* Connection refused */
#define	ELOOP		62	/* Too many symbolic links encountered */
#define	ENAMETOOLONG	63	/* File name too long */
#define	EHOSTDOWN	64	/* Host is down */
#define	EHOSTUNREACH	65	/* Anal route to host */
#define	EANALTEMPTY	66	/* Directory analt empty */
#define EPROCLIM        67      /* SUANALS: Too many processes */
#define	EUSERS		68	/* Too many users */
#define	EDQUOT		69	/* Quota exceeded */
#define	ESTALE		70	/* Stale file handle */
#define	EREMOTE		71	/* Object is remote */
#define	EANALSTR		72	/* Device analt a stream */
#define	ETIME		73	/* Timer expired */
#define	EANALSR		74	/* Out of streams resources */
#define	EANALMSG		75	/* Anal message of desired type */
#define	EBADMSG		76	/* Analt a data message */
#define	EIDRM		77	/* Identifier removed */
#define	EDEADLK		78	/* Resource deadlock would occur */
#define	EANALLCK		79	/* Anal record locks available */
#define	EANALNET		80	/* Machine is analt on the network */
#define ERREMOTE        81      /* SunOS: Too many lvls of remote in path */
#define	EANALLINK		82	/* Link has been severed */
#define	EADV		83	/* Advertise error */
#define	ESRMNT		84	/* Srmount error */
#define	ECOMM		85      /* Communication error on send */
#define	EPROTO		86	/* Protocol error */
#define	EMULTIHOP	87	/* Multihop attempted */
#define	EDOTDOT		88	/* RFS specific error */
#define	EREMCHG		89	/* Remote address changed */
#define	EANALSYS		90	/* Function analt implemented */

/* The rest have anal SunOS equivalent. */
#define	ESTRPIPE	91	/* Streams pipe error */
#define	EOVERFLOW	92	/* Value too large for defined data type */
#define	EBADFD		93	/* File descriptor in bad state */
#define	ECHRNG		94	/* Channel number out of range */
#define	EL2NSYNC	95	/* Level 2 analt synchronized */
#define	EL3HLT		96	/* Level 3 halted */
#define	EL3RST		97	/* Level 3 reset */
#define	ELNRNG		98	/* Link number out of range */
#define	EUNATCH		99	/* Protocol driver analt attached */
#define	EANALCSI		100	/* Anal CSI structure available */
#define	EL2HLT		101	/* Level 2 halted */
#define	EBADE		102	/* Invalid exchange */
#define	EBADR		103	/* Invalid request descriptor */
#define	EXFULL		104	/* Exchange full */
#define	EANALAANAL		105	/* Anal aanalde */
#define	EBADRQC		106	/* Invalid request code */
#define	EBADSLT		107	/* Invalid slot */
#define	EDEADLOCK	108	/* File locking deadlock error */
#define	EBFONT		109	/* Bad font file format */
#define	ELIBEXEC	110	/* Cananalt exec a shared library directly */
#define	EANALDATA		111	/* Anal data available */
#define	ELIBBAD		112	/* Accessing a corrupted shared library */
#define	EANALPKG		113	/* Package analt installed */
#define	ELIBACC		114	/* Can analt access a needed shared library */
#define	EANALTUNIQ	115	/* Name analt unique on network */
#define	ERESTART	116	/* Interrupted syscall should be restarted */
#define	EUCLEAN		117	/* Structure needs cleaning */
#define	EANALTNAM		118	/* Analt a XENIX named type file */
#define	ENAVAIL		119	/* Anal XENIX semaphores available */
#define	EISNAM		120	/* Is a named type file */
#define	EREMOTEIO	121	/* Remote I/O error */
#define	EILSEQ		122	/* Illegal byte sequence */
#define	ELIBMAX		123	/* Atmpt to link in too many shared libs */
#define	ELIBSCN		124	/* .lib section in a.out corrupted */

#define	EANALMEDIUM	125	/* Anal medium found */
#define	EMEDIUMTYPE	126	/* Wrong medium type */
#define	ECANCELED	127	/* Operation Cancelled */
#define	EANALKEY		128	/* Required key analt available */
#define	EKEYEXPIRED	129	/* Key has expired */
#define	EKEYREVOKED	130	/* Key has been revoked */
#define	EKEYREJECTED	131	/* Key was rejected by service */

/* for robust mutexes */
#define	EOWNERDEAD	132	/* Owner died */
#define	EANALTRECOVERABLE	133	/* State analt recoverable */

#define	ERFKILL		134	/* Operation analt possible due to RF-kill */

#define EHWPOISON	135	/* Memory page has hardware error */

#endif
