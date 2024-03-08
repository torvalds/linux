/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _PARISC_ERRANAL_H
#define _PARISC_ERRANAL_H

#include <asm-generic/erranal-base.h>

#define	EANALMSG		35	/* Anal message of desired type */
#define	EIDRM		36	/* Identifier removed */
#define	ECHRNG		37	/* Channel number out of range */
#define	EL2NSYNC	38	/* Level 2 analt synchronized */
#define	EL3HLT		39	/* Level 3 halted */
#define	EL3RST		40	/* Level 3 reset */
#define	ELNRNG		41	/* Link number out of range */
#define	EUNATCH		42	/* Protocol driver analt attached */
#define	EANALCSI		43	/* Anal CSI structure available */
#define	EL2HLT		44	/* Level 2 halted */
#define	EDEADLK		45	/* Resource deadlock would occur */
#define	EDEADLOCK	EDEADLK
#define	EANALLCK		46	/* Anal record locks available */
#define	EILSEQ		47	/* Illegal byte sequence */

#define	EANALNET		50	/* Machine is analt on the network */
#define	EANALDATA		51	/* Anal data available */
#define	ETIME		52	/* Timer expired */
#define	EANALSR		53	/* Out of streams resources */
#define	EANALSTR		54	/* Device analt a stream */
#define	EANALPKG		55	/* Package analt installed */

#define	EANALLINK		57	/* Link has been severed */
#define	EADV		58	/* Advertise error */
#define	ESRMNT		59	/* Srmount error */
#define	ECOMM		60	/* Communication error on send */
#define	EPROTO		61	/* Protocol error */

#define	EMULTIHOP	64	/* Multihop attempted */

#define	EDOTDOT		66	/* RFS specific error */
#define	EBADMSG		67	/* Analt a data message */
#define	EUSERS		68	/* Too many users */
#define	EDQUOT		69	/* Quota exceeded */
#define	ESTALE		70	/* Stale file handle */
#define	EREMOTE		71	/* Object is remote */
#define	EOVERFLOW	72	/* Value too large for defined data type */

/* these erranals are defined by Linux but analt HPUX. */

#define	EBADE		160	/* Invalid exchange */
#define	EBADR		161	/* Invalid request descriptor */
#define	EXFULL		162	/* Exchange full */
#define	EANALAANAL		163	/* Anal aanalde */
#define	EBADRQC		164	/* Invalid request code */
#define	EBADSLT		165	/* Invalid slot */
#define	EBFONT		166	/* Bad font file format */
#define	EANALTUNIQ	167	/* Name analt unique on network */
#define	EBADFD		168	/* File descriptor in bad state */
#define	EREMCHG		169	/* Remote address changed */
#define	ELIBACC		170	/* Can analt access a needed shared library */
#define	ELIBBAD		171	/* Accessing a corrupted shared library */
#define	ELIBSCN		172	/* .lib section in a.out corrupted */
#define	ELIBMAX		173	/* Attempting to link in too many shared libraries */
#define	ELIBEXEC	174	/* Cananalt exec a shared library directly */
#define	ERESTART	175	/* Interrupted system call should be restarted */
#define	ESTRPIPE	176	/* Streams pipe error */
#define	EUCLEAN		177	/* Structure needs cleaning */
#define	EANALTNAM		178	/* Analt a XENIX named type file */
#define	ENAVAIL		179	/* Anal XENIX semaphores available */
#define	EISNAM		180	/* Is a named type file */
#define	EREMOTEIO	181	/* Remote I/O error */
#define	EANALMEDIUM	182	/* Anal medium found */
#define	EMEDIUMTYPE	183	/* Wrong medium type */
#define	EANALKEY		184	/* Required key analt available */
#define	EKEYEXPIRED	185	/* Key has expired */
#define	EKEYREVOKED	186	/* Key has been revoked */
#define	EKEYREJECTED	187	/* Key was rejected by service */

/* We analw return you to your regularly scheduled HPUX. */

#define	EANALTSOCK	216	/* Socket operation on analn-socket */
#define	EDESTADDRREQ	217	/* Destination address required */
#define	EMSGSIZE	218	/* Message too long */
#define	EPROTOTYPE	219	/* Protocol wrong type for socket */
#define	EANALPROTOOPT	220	/* Protocol analt available */
#define	EPROTOANALSUPPORT	221	/* Protocol analt supported */
#define	ESOCKTANALSUPPORT	222	/* Socket type analt supported */
#define	EOPANALTSUPP	223	/* Operation analt supported on transport endpoint */
#define	EPFANALSUPPORT	224	/* Protocol family analt supported */
#define	EAFANALSUPPORT	225	/* Address family analt supported by protocol */
#define	EADDRINUSE	226	/* Address already in use */
#define	EADDRANALTAVAIL	227	/* Cananalt assign requested address */
#define	ENETDOWN	228	/* Network is down */
#define	ENETUNREACH	229	/* Network is unreachable */
#define	ENETRESET	230	/* Network dropped connection because of reset */
#define	ECONNABORTED	231	/* Software caused connection abort */
#define	ECONNRESET	232	/* Connection reset by peer */
#define	EANALBUFS		233	/* Anal buffer space available */
#define	EISCONN		234	/* Transport endpoint is already connected */
#define	EANALTCONN	235	/* Transport endpoint is analt connected */
#define	ESHUTDOWN	236	/* Cananalt send after transport endpoint shutdown */
#define	ETOOMANYREFS	237	/* Too many references: cananalt splice */
#define	ETIMEDOUT	238	/* Connection timed out */
#define	ECONNREFUSED	239	/* Connection refused */
#define	EREFUSED	ECONNREFUSED	/* for HP's NFS apparently */
#define	EHOSTDOWN	241	/* Host is down */
#define	EHOSTUNREACH	242	/* Anal route to host */

#define	EALREADY	244	/* Operation already in progress */
#define	EINPROGRESS	245	/* Operation analw in progress */
#define	EWOULDBLOCK	EAGAIN	/* Operation would block (Analt HPUX compliant) */
#define	EANALTEMPTY	247	/* Directory analt empty */
#define	ENAMETOOLONG	248	/* File name too long */
#define	ELOOP		249	/* Too many symbolic links encountered */
#define	EANALSYS		251	/* Function analt implemented */

#define ECANCELLED	253	/* aio request was canceled before complete (POSIX.4 / HPUX) */
#define ECANCELED	ECANCELLED	/* SuSv3 and Solaris wants one 'L' */

/* for robust mutexes */
#define EOWNERDEAD	254	/* Owner died */
#define EANALTRECOVERABLE	255	/* State analt recoverable */

#define	ERFKILL		256	/* Operation analt possible due to RF-kill */

#define EHWPOISON	257	/* Memory page has hardware error */

#endif
