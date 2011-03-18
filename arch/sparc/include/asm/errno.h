#ifndef _SPARC_ERRNO_H
#define _SPARC_ERRNO_H

/* These match the SunOS error numbering scheme. */

#include <asm-generic/errno-base.h>

#define	EWOULDBLOCK	EAGAIN	/* Operation would block */
#define	EINPROGRESS	36	/* Operation now in progress */
#define	EALREADY	37	/* Operation already in progress */
#define	ENOTSOCK	38	/* Socket operation on non-socket */
#define	EDESTADDRREQ	39	/* Destination address required */
#define	EMSGSIZE	40	/* Message too long */
#define	EPROTOTYPE	41	/* Protocol wrong type for socket */
#define	ENOPROTOOPT	42	/* Protocol not available */
#define	EPROTONOSUPPORT	43	/* Protocol not supported */
#define	ESOCKTNOSUPPORT	44	/* Socket type not supported */
#define	EOPNOTSUPP	45	/* Op not supported on transport endpoint */
#define	EPFNOSUPPORT	46	/* Protocol family not supported */
#define	EAFNOSUPPORT	47	/* Address family not supported by protocol */
#define	EADDRINUSE	48	/* Address already in use */
#define	EADDRNOTAVAIL	49	/* Cannot assign requested address */
#define	ENETDOWN	50	/* Network is down */
#define	ENETUNREACH	51	/* Network is unreachable */
#define	ENETRESET	52	/* Net dropped connection because of reset */
#define	ECONNABORTED	53	/* Software caused connection abort */
#define	ECONNRESET	54	/* Connection reset by peer */
#define	ENOBUFS		55	/* No buffer space available */
#define	EISCONN		56	/* Transport endpoint is already connected */
#define	ENOTCONN	57	/* Transport endpoint is not connected */
#define	ESHUTDOWN	58	/* No send after transport endpoint shutdown */
#define	ETOOMANYREFS	59	/* Too many references: cannot splice */
#define	ETIMEDOUT	60	/* Connection timed out */
#define	ECONNREFUSED	61	/* Connection refused */
#define	ELOOP		62	/* Too many symbolic links encountered */
#define	ENAMETOOLONG	63	/* File name too long */
#define	EHOSTDOWN	64	/* Host is down */
#define	EHOSTUNREACH	65	/* No route to host */
#define	ENOTEMPTY	66	/* Directory not empty */
#define EPROCLIM        67      /* SUNOS: Too many processes */
#define	EUSERS		68	/* Too many users */
#define	EDQUOT		69	/* Quota exceeded */
#define	ESTALE		70	/* Stale NFS file handle */
#define	EREMOTE		71	/* Object is remote */
#define	ENOSTR		72	/* Device not a stream */
#define	ETIME		73	/* Timer expired */
#define	ENOSR		74	/* Out of streams resources */
#define	ENOMSG		75	/* No message of desired type */
#define	EBADMSG		76	/* Not a data message */
#define	EIDRM		77	/* Identifier removed */
#define	EDEADLK		78	/* Resource deadlock would occur */
#define	ENOLCK		79	/* No record locks available */
#define	ENONET		80	/* Machine is not on the network */
#define ERREMOTE        81      /* SunOS: Too many lvls of remote in path */
#define	ENOLINK		82	/* Link has been severed */
#define	EADV		83	/* Advertise error */
#define	ESRMNT		84	/* Srmount error */
#define	ECOMM		85      /* Communication error on send */
#define	EPROTO		86	/* Protocol error */
#define	EMULTIHOP	87	/* Multihop attempted */
#define	EDOTDOT		88	/* RFS specific error */
#define	EREMCHG		89	/* Remote address changed */
#define	ENOSYS		90	/* Function not implemented */

/* The rest have no SunOS equivalent. */
#define	ESTRPIPE	91	/* Streams pipe error */
#define	EOVERFLOW	92	/* Value too large for defined data type */
#define	EBADFD		93	/* File descriptor in bad state */
#define	ECHRNG		94	/* Channel number out of range */
#define	EL2NSYNC	95	/* Level 2 not synchronized */
#define	EL3HLT		96	/* Level 3 halted */
#define	EL3RST		97	/* Level 3 reset */
#define	ELNRNG		98	/* Link number out of range */
#define	EUNATCH		99	/* Protocol driver not attached */
#define	ENOCSI		100	/* No CSI structure available */
#define	EL2HLT		101	/* Level 2 halted */
#define	EBADE		102	/* Invalid exchange */
#define	EBADR		103	/* Invalid request descriptor */
#define	EXFULL		104	/* Exchange full */
#define	ENOANO		105	/* No anode */
#define	EBADRQC		106	/* Invalid request code */
#define	EBADSLT		107	/* Invalid slot */
#define	EDEADLOCK	108	/* File locking deadlock error */
#define	EBFONT		109	/* Bad font file format */
#define	ELIBEXEC	110	/* Cannot exec a shared library directly */
#define	ENODATA		111	/* No data available */
#define	ELIBBAD		112	/* Accessing a corrupted shared library */
#define	ENOPKG		113	/* Package not installed */
#define	ELIBACC		114	/* Can not access a needed shared library */
#define	ENOTUNIQ	115	/* Name not unique on network */
#define	ERESTART	116	/* Interrupted syscall should be restarted */
#define	EUCLEAN		117	/* Structure needs cleaning */
#define	ENOTNAM		118	/* Not a XENIX named type file */
#define	ENAVAIL		119	/* No XENIX semaphores available */
#define	EISNAM		120	/* Is a named type file */
#define	EREMOTEIO	121	/* Remote I/O error */
#define	EILSEQ		122	/* Illegal byte sequence */
#define	ELIBMAX		123	/* Atmpt to link in too many shared libs */
#define	ELIBSCN		124	/* .lib section in a.out corrupted */

#define	ENOMEDIUM	125	/* No medium found */
#define	EMEDIUMTYPE	126	/* Wrong medium type */
#define	ECANCELED	127	/* Operation Cancelled */
#define	ENOKEY		128	/* Required key not available */
#define	EKEYEXPIRED	129	/* Key has expired */
#define	EKEYREVOKED	130	/* Key has been revoked */
#define	EKEYREJECTED	131	/* Key was rejected by service */

/* for robust mutexes */
#define	EOWNERDEAD	132	/* Owner died */
#define	ENOTRECOVERABLE	133	/* State not recoverable */

#define	ERFKILL		134	/* Operation not possible due to RF-kill */

#define EHWPOISON	135	/* Memory page has hardware error */

#endif
