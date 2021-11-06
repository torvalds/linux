/*
 * Misc useful os-independent macros and functions.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef	_bcmutils_h_
#define	_bcmutils_h_

#include <bcmtlv.h>

/* For now, protect the bcmerror.h */
#ifdef BCMUTILS_ERR_CODES
#include <bcmerror.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define bcm_strncpy_s(dst, noOfElements, src, count)    strncpy((dst), (src), (count))
#ifdef FREEBSD
#define bcm_strncat_s(dst, noOfElements, src, count)    strcat((dst), (src))
#else
#define bcm_strncat_s(dst, noOfElements, src, count)    strncat((dst), (src), (count))
#endif /* FREEBSD */
#define bcm_snprintf_s snprintf
#define bcm_sprintf_s snprintf

/*
 * #define bcm_strcpy_s(dst, count, src)            strncpy((dst), (src), (count))
 * Use bcm_strcpy_s instead as it is a safer option
 * bcm_strcat_s: Use bcm_strncat_s as a safer option
 *
 */

#define BCM_BIT(x)		(1u << (x))
/* useful to count number of set bit in x */
#define BCM_CLR_FISRT_BIT(x) ((x - 1) & x)
/* first bit set in x. Useful to iterate through a mask */
#define BCM_FIRST_BIT(x) (BCM_CLR_FISRT_BIT(x)^(x))

/* Macro to iterate through the set bits in mask.
 * NOTE: the argument "mask" will be cleared after
 * the iteration.
 */

#define FOREACH_BIT(c, mask)\
	for (c = BCM_FIRST_BIT(mask); mask != 0; \
		 mask = BCM_CLR_FISRT_BIT(mask), c = BCM_FIRST_BIT(mask))

/* ctype replacement */
#define _BCM_U	0x01	/* upper */
#define _BCM_L	0x02	/* lower */
#define _BCM_D	0x04	/* digit */
#define _BCM_C	0x08	/* cntrl */
#define _BCM_P	0x10	/* punct */
#define _BCM_S	0x20	/* white space (space/lf/tab) */
#define _BCM_X	0x40	/* hex digit */
#define _BCM_SP	0x80	/* hard space (0x20) */

extern const unsigned char bcm_ctype[256];
#define bcm_ismask(x)	(bcm_ctype[(unsigned char)(x)])

#define bcm_isalnum(c)	((bcm_ismask(c)&(_BCM_U|_BCM_L|_BCM_D)) != 0)
#define bcm_isalpha(c)	((bcm_ismask(c)&(_BCM_U|_BCM_L)) != 0)
#define bcm_iscntrl(c)	((bcm_ismask(c)&(_BCM_C)) != 0)
#define bcm_isdigit(c)	((bcm_ismask(c)&(_BCM_D)) != 0)
#define bcm_isgraph(c)	((bcm_ismask(c)&(_BCM_P|_BCM_U|_BCM_L|_BCM_D)) != 0)
#define bcm_islower(c)	((bcm_ismask(c)&(_BCM_L)) != 0)
#define bcm_isprint(c)	((bcm_ismask(c)&(_BCM_P|_BCM_U|_BCM_L|_BCM_D|_BCM_SP)) != 0)
#define bcm_ispunct(c)	((bcm_ismask(c)&(_BCM_P)) != 0)
#define bcm_isspace(c)	((bcm_ismask(c)&(_BCM_S)) != 0)
#define bcm_isupper(c)	((bcm_ismask(c)&(_BCM_U)) != 0)
#define bcm_isxdigit(c)	((bcm_ismask(c)&(_BCM_D|_BCM_X)) != 0)
#define bcm_tolower(c)	(bcm_isupper((c)) ? ((c) + 'a' - 'A') : (c))
#define bcm_toupper(c)	(bcm_islower((c)) ? ((c) + 'A' - 'a') : (c))

#define CIRCULAR_ARRAY_FULL(rd_idx, wr_idx, max) ((wr_idx + 1)%max == rd_idx)

#define KB(bytes)	(((bytes) + 1023) / 1024)

/* Buffer structure for collecting string-formatted data
* using bcm_bprintf() API.
* Use bcm_binit() to initialize before use
*/

struct bcmstrbuf {
	char *buf;	/* pointer to current position in origbuf */
	unsigned int size;	/* current (residual) size in bytes */
	char *origbuf;	/* unmodified pointer to orignal buffer */
	unsigned int origsize;	/* unmodified orignal buffer size in bytes */
};

#define BCMSTRBUF_LEN(b)	(b->size)
#define BCMSTRBUF_BUF(b)	(b->buf)

struct ether_addr;
extern char *bcm_ether_ntoa(const struct ether_addr *ea, char *buf);
extern int bcm_ether_atoe(const char *p, struct ether_addr *ea);

/* ** driver-only section ** */
#ifdef BCMDRIVER

#include <osl.h>
#include <hnd_pktq.h>
#include <hnd_pktpool.h>

#define GPIO_PIN_NOTDEFINED	0x20	/* Pin not defined */

/*
 * Spin at most 'us' microseconds while 'exp' is true.
 * Caller should explicitly test 'exp' when this completes
 * and take appropriate error action if 'exp' is still true.
 */
#ifndef SPINWAIT_POLL_PERIOD
#define SPINWAIT_POLL_PERIOD	10U
#endif

#ifdef BCMFUZZ
/* fake spinwait for fuzzing */
#define SPINWAIT(exp, us) { \
	uint countdown = (exp) != 0 ? 1 : 0; \
	while (countdown > 0) { \
		countdown--; \
	} \
}

#elif defined(PHY_REG_TRACE_FRAMEWORK)
#include <phy_utils_log_api.h>
#define SPINWAIT(exp, us) { \
	uint countdown = (us) + (SPINWAIT_POLL_PERIOD - 1U); \
	phy_utils_log_spinwait_start(); \
	while (((exp) != 0) && (uint)(countdown >= SPINWAIT_POLL_PERIOD)) { \
		OSL_DELAY(SPINWAIT_POLL_PERIOD); \
		countdown -= SPINWAIT_POLL_PERIOD; \
	} \
	phy_utils_log_spinwait_end(us, countdown); \
}

#else
#define SPINWAIT(exp, us) { \
	uint countdown = (us) + (SPINWAIT_POLL_PERIOD - 1U); \
	while (((exp) != 0) && (uint)(countdown >= SPINWAIT_POLL_PERIOD)) { \
		OSL_DELAY(SPINWAIT_POLL_PERIOD); \
		countdown -= SPINWAIT_POLL_PERIOD; \
	} \
}
#endif /* BCMFUZZ */

/* forward definition of ether_addr structure used by some function prototypes */

extern int ether_isbcast(const void *ea);
extern int ether_isnulladdr(const void *ea);

#define UP_TABLE_MAX	((IPV4_TOS_DSCP_MASK >> IPV4_TOS_DSCP_SHIFT) + 1)	/* 64 max */
#define CORE_SLAVE_PORT_0	0
#define CORE_SLAVE_PORT_1	1
#define CORE_BASE_ADDR_0	0
#define CORE_BASE_ADDR_1	1

#ifdef DONGLEBUILD
/* TRIM Tail bytes from lfrag */
extern void pktfrag_trim_tailbytes(osl_t * osh, void* p, uint16 len, uint8 type);
#define PKTFRAG_TRIM_TAILBYTES(osh, p, len, type)	pktfrag_trim_tailbytes(osh, p, len, type)
#else
#define PKTFRAG_TRIM_TAILBYTES(osh, p, len, type)	PKTSETLEN(osh, p, PKTLEN(osh, p) - len)
#endif /* DONGLEBUILD */

/* externs */
/* packet */
extern uint pktcopy(osl_t *osh, void *p, uint offset, uint len, uchar *buf);
extern uint pktfrombuf(osl_t *osh, void *p, uint offset, uint len, uchar *buf);
extern uint pkttotlen(osl_t *osh, void *p);
extern uint pkttotcnt(osl_t *osh, void *p);
extern void *pktlast(osl_t *osh, void *p);
extern uint pktsegcnt(osl_t *osh, void *p);
extern uint8 *pktdataoffset(osl_t *osh, void *p,  uint offset);
extern void *pktoffset(osl_t *osh, void *p,  uint offset);
/* Add to adjust 802.1x priority */
extern void pktset8021xprio(void *pkt, int prio);

#ifdef WLCSO
extern uint pkttotlen_no_sfhtoe_hdr(osl_t *osh, void *p, uint toe_hdr_len);
#else
#define pkttotlen_no_sfhtoe_hdr(osh, p, hdrlen)	pkttotlen(osh, p)
#endif /* WLCSO */

/* Get priority from a packet and pass it back in scb (or equiv) */
#define	PKTPRIO_VDSCP	0x100u		/* DSCP prio found after VLAN tag */
#define	PKTPRIO_VLAN	0x200u		/* VLAN prio found */
#define	PKTPRIO_UPD	0x400u		/* DSCP used to update VLAN prio */
#define	PKTPRIO_DSCP	0x800u		/* DSCP prio found */

/* DSCP type definitions (RFC4594) */
/* AF1x: High-Throughput Data (RFC2597) */
#define DSCP_AF11	0x0Au
#define DSCP_AF12	0x0Cu
#define DSCP_AF13	0x0Eu
/* AF2x: Low-Latency Data (RFC2597) */
#define DSCP_AF21	0x12u
#define DSCP_AF22	0x14u
#define DSCP_AF23	0x16u
/* CS2: OAM (RFC2474) */
#define DSCP_CS2	0x10u
/* AF3x: Multimedia Streaming (RFC2597) */
#define DSCP_AF31	0x1Au
#define DSCP_AF32	0x1Cu
#define DSCP_AF33	0x1Eu
/* CS3: Broadcast Video (RFC2474) */
#define DSCP_CS3	0x18u
/* VA: VOCIE-ADMIT (RFC5865) */
#define DSCP_VA		0x2Cu
/* EF: Telephony (RFC3246) */
#define DSCP_EF		0x2Eu
/* CS6: Network Control (RFC2474) */
#define DSCP_CS6	0x30u
/* CS7: Network Control (RFC2474) */
#define DSCP_CS7	0x38u

extern uint pktsetprio(void *pkt, bool update_vtag);
extern uint pktsetprio_qms(void *pkt, uint8* up_table, bool update_vtag);
extern bool pktgetdscp(uint8 *pktdata, uint pktlen, uint8 *dscp);

/* ethernet address */
extern uint64 bcm_ether_ntou64(const struct ether_addr *ea) BCMCONSTFN;
extern int bcm_addrmask_set(int enable);
extern int bcm_addrmask_get(int *val);

/* ip address */
struct ipv4_addr;
extern char *bcm_ip_ntoa(struct ipv4_addr *ia, char *buf);
extern char *bcm_ipv6_ntoa(void *ipv6, char *buf);
extern int bcm_atoipv4(const char *p, struct ipv4_addr *ip);

/* delay */
extern void bcm_mdelay(uint ms);
/* variable access */
#if defined(BCM_RECLAIM)
extern bool _nvram_reclaim_enb;
#define NVRAM_RECLAIM_ENAB() (_nvram_reclaim_enb)
#ifdef BCMDBG
#define NVRAM_RECLAIM_CHECK(name)							\
	if (NVRAM_RECLAIM_ENAB() && (bcm_attach_part_reclaimed == TRUE)) {		\
		printf("NVRAM already reclaimed, %s\n", (name));			\
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_NULL_DEREF();				\
		*(char*) 0 = 0; /* TRAP */						\
		GCC_DIAGNOSTIC_POP();							\
		return NULL;								\
	}
#else /* BCMDBG */
#define NVRAM_RECLAIM_CHECK(name)							\
	if (NVRAM_RECLAIM_ENAB() && (bcm_attach_part_reclaimed == TRUE)) {		\
		GCC_DIAGNOSTIC_PUSH_SUPPRESS_NULL_DEREF();				\
		*(char*) 0 = 0; /* TRAP */						\
		GCC_DIAGNOSTIC_POP();							\
		return NULL;								\
	}
#endif /* BCMDBG */
#else /* BCM_RECLAIM */
#define NVRAM_RECLAIM_CHECK(name)
#endif /* BCM_RECLAIM */

#ifdef WL_FWSIGN
#define getvar(vars, name)			(NULL)
#define getintvar(vars, name)			(0)
#define getintvararray(vars, name, index)	(0)
#define getintvararraysize(vars, name)		(0)
#else /* WL_FWSIGN */
extern char *getvar(char *vars, const char *name);
extern int getintvar(char *vars, const char *name);
extern int getintvararray(char *vars, const char *name, int index);
extern int getintvararraysize(char *vars, const char *name);
#endif /* WL_FWSIGN */

/* Read an array of values from a possibly slice-specific nvram string */
extern int get_uint8_vararray_slicespecific(osl_t *osh, char *vars, char *vars_table_accessor,
	const char* name, uint8* dest_array, uint dest_size);
extern int get_int16_vararray_slicespecific(osl_t *osh, char *vars, char *vars_table_accessor,
	const char* name, int16* dest_array, uint dest_size);
/* Prepend a slice-specific accessor to an nvram string name */
extern uint get_slicespecific_var_name(osl_t *osh, char *vars_table_accessor,
	const char *name, char **name_out);

extern uint getgpiopin(char *vars, char *pin_name, uint def_pin);
#ifdef BCMDBG
extern void prpkt(const char *msg, osl_t *osh, void *p0);
#endif /* BCMDBG */
#ifdef BCMPERFSTATS
extern void bcm_perf_enable(void);
extern void bcmstats(char *fmt);
extern void bcmlog(char *fmt, uint a1, uint a2);
extern void bcmdumplog(char *buf, int size);
extern int bcmdumplogent(char *buf, uint idx);
#else
#define bcm_perf_enable()
#define bcmstats(fmt)
#define	bcmlog(fmt, a1, a2)
#define	bcmdumplog(buf, size)	*buf = '\0'
#define	bcmdumplogent(buf, idx)	-1
#endif /* BCMPERFSTATS */

#define TSF_TICKS_PER_MS	1000
#define TS_ENTER		0xdeadbeef	/* Timestamp profiling enter */
#define TS_EXIT			0xbeefcafe	/* Timestamp profiling exit */

#if defined(BCMTSTAMPEDLOGS)
/* Store a TSF timestamp and a log line in the log buffer */
extern void bcmtslog(uint32 tstamp, const char *fmt, uint a1, uint a2);
/* Print out the log buffer with timestamps */
extern void bcmprinttslogs(void);
/* Print out a microsecond timestamp as "sec.ms.us " */
extern void bcmprinttstamp(uint32 us);
/* Dump to buffer a microsecond timestamp as "sec.ms.us " */
extern void bcmdumptslog(struct bcmstrbuf *b);
#else
#define bcmtslog(tstamp, fmt, a1, a2)
#define bcmprinttslogs()
#define bcmprinttstamp(us)
#define bcmdumptslog(b)
#endif /* BCMTSTAMPEDLOGS */

bool bcm_match_buffers(const uint8 *b1, uint b1_len, const uint8 *b2, uint b2_len);

/* Support for sharing code across in-driver iovar implementations.
 * The intent is that a driver use this structure to map iovar names
 * to its (private) iovar identifiers, and the lookup function to
 * find the entry.  Macros are provided to map ids and get/set actions
 * into a single number space for a switch statement.
 */

/* iovar structure */
typedef struct bcm_iovar {
	const char *name;	/* name for lookup and display */
	uint16 varid;		/* id for switch */
	uint16 flags;		/* driver-specific flag bits */
	uint8 flags2;		 /* driver-specific flag bits */
	uint8 type;		/* base type of argument */
	uint16 minlen;		/* min length for buffer vars */
} bcm_iovar_t;

/* varid definitions are per-driver, may use these get/set bits */

/* IOVar action bits for id mapping */
#define IOV_GET 0 /* Get an iovar */
#define IOV_SET 1 /* Set an iovar */

/* Varid to actionid mapping */
#define IOV_GVAL(id)		((id) * 2)
#define IOV_SVAL(id)		((id) * 2 + IOV_SET)
#define IOV_ISSET(actionid)	((actionid & IOV_SET) == IOV_SET)
#define IOV_ID(actionid)	(actionid >> 1)

/* flags are per-driver based on driver attributes */

extern const bcm_iovar_t *bcm_iovar_lookup(const bcm_iovar_t *table, const char *name);
extern int bcm_iovar_lencheck(const bcm_iovar_t *table, void *arg, uint len, bool set);

/* ioctl structure */
typedef struct wlc_ioctl_cmd {
	uint16 cmd;			/**< IOCTL command */
	uint16 flags;			/**< IOCTL command flags */
	uint16 min_len;			/**< IOCTL command minimum argument len (in bytes) */
} wlc_ioctl_cmd_t;

#if defined(WLTINYDUMP) || defined(BCMDBG) || defined(WLMSG_INFORM) || \
	defined(WLMSG_ASSOC) || defined(WLMSG_PRPKT) || defined(WLMSG_WSEC)
extern int bcm_format_ssid(char* buf, const uchar ssid[], uint ssid_len);
#endif /* WLTINYDUMP || BCMDBG || WLMSG_INFORM || WLMSG_ASSOC || WLMSG_PRPKT */
#endif	/* BCMDRIVER */

/* string */
extern int bcm_atoi(const char *s);
extern ulong bcm_strtoul(const char *cp, char **endp, uint base);
extern uint64 bcm_strtoull(const char *cp, char **endp, uint base);
extern char *bcmstrstr(const char *haystack, const char *needle);
extern char *bcmstrnstr(const char *s, uint s_len, const char *substr, uint substr_len);
extern char *bcmstrcat(char *dest, const char *src);
extern char *bcmstrncat(char *dest, const char *src, uint size);
extern ulong wchar2ascii(char *abuf, ushort *wbuf, ushort wbuflen, ulong abuflen);
char* bcmstrtok(char **string, const char *delimiters, char *tokdelim);
int bcmstricmp(const char *s1, const char *s2);
int bcmstrnicmp(const char* s1, const char* s2, int cnt);
uint16 bcmhex2bin(const uint8* hex, uint hex_len, uint8 *buf, uint buf_len);

/* Base type definitions */
#define IOVT_VOID	0	/* no value (implictly set only) */
#define IOVT_BOOL	1	/* any value ok (zero/nonzero) */
#define IOVT_INT8	2	/* integer values are range-checked */
#define IOVT_UINT8	3	/* unsigned int 8 bits */
#define IOVT_INT16	4	/* int 16 bits */
#define IOVT_UINT16	5	/* unsigned int 16 bits */
#define IOVT_INT32	6	/* int 32 bits */
#define IOVT_UINT32	7	/* unsigned int 32 bits */
#define IOVT_BUFFER	8	/* buffer is size-checked as per minlen */
#define BCM_IOVT_VALID(type) (((unsigned int)(type)) <= IOVT_BUFFER)

/* Initializer for IOV type strings */
#define BCM_IOV_TYPE_INIT { \
	"void", \
	"bool", \
	"int8", \
	"uint8", \
	"int16", \
	"uint16", \
	"int32", \
	"uint32", \
	"buffer", \
	"" }

#define BCM_IOVT_IS_INT(type) (\
	(type == IOVT_BOOL) || \
	(type == IOVT_INT8) || \
	(type == IOVT_UINT8) || \
	(type == IOVT_INT16) || \
	(type == IOVT_UINT16) || \
	(type == IOVT_INT32) || \
	(type == IOVT_UINT32))

/* ** driver/apps-shared section ** */

#define BCME_STRLEN             64      /* Max string length for BCM errors */
#define VALID_BCMERROR(e)       valid_bcmerror(e)

#ifdef DBG_BUS
/** tracks non typical execution paths, use gdb with arm sim + firmware dump to read counters */
#define DBG_BUS_INC(s, cnt) ((s)->dbg_bus->cnt++)
#else
#define DBG_BUS_INC(s, cnt)
#endif /* DBG_BUS */

/* BCMUTILS_ERR_CODES is defined to use the error codes from bcmerror.h
 * otherwise use from this file.
 */
#ifndef BCMUTILS_ERR_CODES

/*
 * error codes could be added but the defined ones shouldn't be changed/deleted
 * these error codes are exposed to the user code
 * when ever a new error code is added to this list
 * please update errorstring table with the related error string and
 * update osl files with os specific errorcode map
*/

#define BCME_OK				0	/* Success */
#define BCME_ERROR			-1	/* Error generic */
#define BCME_BADARG			-2	/* Bad Argument */
#define BCME_BADOPTION			-3	/* Bad option */
#define BCME_NOTUP			-4	/* Not up */
#define BCME_NOTDOWN			-5	/* Not down */
#define BCME_NOTAP			-6	/* Not AP */
#define BCME_NOTSTA			-7	/* Not STA  */
#define BCME_BADKEYIDX			-8	/* BAD Key Index */
#define BCME_RADIOOFF			-9	/* Radio Off */
#define BCME_NOTBANDLOCKED		-10	/* Not  band locked */
#define BCME_NOCLK			-11	/* No Clock */
#define BCME_BADRATESET			-12	/* BAD Rate valueset */
#define BCME_BADBAND			-13	/* BAD Band */
#define BCME_BUFTOOSHORT		-14	/* Buffer too short */
#define BCME_BUFTOOLONG			-15	/* Buffer too long */
#define BCME_BUSY			-16	/* Busy */
#define BCME_NOTASSOCIATED		-17	/* Not Associated */
#define BCME_BADSSIDLEN			-18	/* Bad SSID len */
#define BCME_OUTOFRANGECHAN		-19	/* Out of Range Channel */
#define BCME_BADCHAN			-20	/* Bad Channel */
#define BCME_BADADDR			-21	/* Bad Address */
#define BCME_NORESOURCE			-22	/* Not Enough Resources */
#define BCME_UNSUPPORTED		-23	/* Unsupported */
#define BCME_BADLEN			-24	/* Bad length */
#define BCME_NOTREADY			-25	/* Not Ready */
#define BCME_EPERM			-26	/* Not Permitted */
#define BCME_NOMEM			-27	/* No Memory */
#define BCME_ASSOCIATED			-28	/* Associated */
#define BCME_RANGE			-29	/* Not In Range */
#define BCME_NOTFOUND			-30	/* Not Found */
#define BCME_WME_NOT_ENABLED		-31	/* WME Not Enabled */
#define BCME_TSPEC_NOTFOUND		-32	/* TSPEC Not Found */
#define BCME_ACM_NOTSUPPORTED		-33	/* ACM Not Supported */
#define BCME_NOT_WME_ASSOCIATION	-34	/* Not WME Association */
#define BCME_SDIO_ERROR			-35	/* SDIO Bus Error */
#define BCME_DONGLE_DOWN		-36	/* Dongle Not Accessible */
#define BCME_VERSION			-37	/* Incorrect version */
#define BCME_TXFAIL			-38	/* TX failure */
#define BCME_RXFAIL			-39	/* RX failure */
#define BCME_NODEVICE			-40	/* Device not present */
#define BCME_NMODE_DISABLED		-41	/* NMODE disabled */
#define BCME_MSCH_DUP_REG		-42	/* Duplicate slot registration */
#define BCME_SCANREJECT			-43	/* reject scan request */
#define BCME_USAGE_ERROR		-44	/* WLCMD usage error */
#define BCME_IOCTL_ERROR		-45	/* WLCMD ioctl error */
#define BCME_SERIAL_PORT_ERR		-46	/* RWL serial port error */
#define BCME_DISABLED			-47	/* Disabled in this build */
#define BCME_DECERR			-48	/* Decrypt error */
#define BCME_ENCERR			-49	/* Encrypt error */
#define BCME_MICERR			-50	/* Integrity/MIC error */
#define BCME_REPLAY			-51	/* Replay */
#define BCME_IE_NOTFOUND		-52	/* IE not found */
#define BCME_DATA_NOTFOUND		-53	/* Complete data not found in buffer */
#define BCME_NOT_GC			-54	/* expecting a group client */
#define BCME_PRS_REQ_FAILED		-55	/* GC presence req failed to sent */
#define BCME_NO_P2P_SE			-56	/* Could not find P2P-Subelement */
#define BCME_NOA_PND			-57	/* NoA pending, CB shuld be NULL */
#define BCME_FRAG_Q_FAILED		-58	/* queueing 80211 frag failedi */
#define BCME_GET_AF_FAILED		-59	/* Get p2p AF pkt failed */
#define BCME_MSCH_NOTREADY		-60	/* scheduler not ready */
#define BCME_IOV_LAST_CMD		-61	/* last batched iov sub-command */
#define BCME_MINIPMU_CAL_FAIL		-62	/* MiniPMU cal failed */
#define BCME_RCAL_FAIL			-63	/* Rcal failed */
#define BCME_LPF_RCCAL_FAIL		-64	/* RCCAL failed */
#define BCME_DACBUF_RCCAL_FAIL		-65	/* RCCAL failed */
#define BCME_VCOCAL_FAIL		-66	/* VCOCAL failed */
#define BCME_BANDLOCKED			-67	/* interface is restricted to a band */
#define BCME_BAD_IE_DATA		-68	/* Recieved ie with invalid/bad data */
#define BCME_REG_FAILED			-69	/* Generic registration failed */
#define BCME_NOCHAN			-70	/* Registration with 0 chans in list */
#define BCME_PKTTOSS			-71	/* Pkt tossed */
#define BCME_DNGL_DEVRESET		-72	/* dongle re-attach during DEVRESET */
#define BCME_ROAM			-73	/* Roam related failures */
#define BCME_NO_SIG_FILE		-74	/* Signature file is missing */

#define BCME_LAST			BCME_NO_SIG_FILE

#define BCME_NOTENABLED BCME_DISABLED

/* This error code is *internal* to the driver, and is not propogated to users. It should
 * only be used by IOCTL patch handlers as an indication that it did not handle the IOCTL.
 * (Since the error code is internal, an entry in 'BCMERRSTRINGTABLE' is not required,
 * nor does it need to be part of any OSL driver-to-OS error code mapping).
 */
#define BCME_IOCTL_PATCH_UNSUPPORTED	-9999
#if (BCME_LAST <= BCME_IOCTL_PATCH_UNSUPPORTED)
	#error "BCME_LAST <= BCME_IOCTL_PATCH_UNSUPPORTED"
#endif

/* These are collection of BCME Error strings */
#define BCMERRSTRINGTABLE {		\
	"OK",				\
	"Undefined error",		\
	"Bad Argument",			\
	"Bad Option",			\
	"Not up",			\
	"Not down",			\
	"Not AP",			\
	"Not STA",			\
	"Bad Key Index",		\
	"Radio Off",			\
	"Not band locked",		\
	"No clock",			\
	"Bad Rate valueset",		\
	"Bad Band",			\
	"Buffer too short",		\
	"Buffer too long",		\
	"Busy",				\
	"Not Associated",		\
	"Bad SSID len",			\
	"Out of Range Channel",		\
	"Bad Channel",			\
	"Bad Address",			\
	"Not Enough Resources",		\
	"Unsupported",			\
	"Bad length",			\
	"Not Ready",			\
	"Not Permitted",		\
	"No Memory",			\
	"Associated",			\
	"Not In Range",			\
	"Not Found",			\
	"WME Not Enabled",		\
	"TSPEC Not Found",		\
	"ACM Not Supported",		\
	"Not WME Association",		\
	"SDIO Bus Error",		\
	"Dongle Not Accessible",	\
	"Incorrect version",		\
	"TX Failure",			\
	"RX Failure",			\
	"Device Not Present",		\
	"NMODE Disabled",		\
	"Host Offload in device",	\
	"Scan Rejected",		\
	"WLCMD usage error",		\
	"WLCMD ioctl error",		\
	"RWL serial port error",	\
	"Disabled",			\
	"Decrypt error",		\
	"Encrypt error",		\
	"MIC error",			\
	"Replay",			\
	"IE not found",			\
	"Data not found",		\
	"NOT GC",			\
	"PRS REQ FAILED",		\
	"NO P2P SubElement",		\
	"NOA Pending",			\
	"FRAG Q FAILED",		\
	"GET ActionFrame failed",	\
	"scheduler not ready",		\
	"Last IOV batched sub-cmd",	\
	"Mini PMU Cal failed",		\
	"R-cal failed",			\
	"LPF RC Cal failed",		\
	"DAC buf RC Cal failed",	\
	"VCO Cal failed",		\
	"band locked",			\
	"Recieved ie with invalid data", \
	"registration failed",		\
	"Registration with zero channels", \
	"pkt toss",			\
	"Dongle Devreset",		\
	"Critical roam in progress",	\
	"Signature file is missing",	\
}
#endif	/* BCMUTILS_ERR_CODES */

#ifndef ABS
#define	ABS(a)			(((a) < 0) ? -(a) : (a))
#endif /* ABS */

#ifndef MIN
#define	MIN(a, b)		(((a) < (b)) ? (a) : (b))
#endif /* MIN */

#ifndef MAX
#define	MAX(a, b)		(((a) > (b)) ? (a) : (b))
#endif /* MAX */

/* limit to [min, max] */
#ifndef LIMIT_TO_RANGE
#define LIMIT_TO_RANGE(x, min, max) \
	((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif /* LIMIT_TO_RANGE */

/* limit to  max */
#ifndef LIMIT_TO_MAX
#define LIMIT_TO_MAX(x, max) \
	(((x) > (max) ? (max) : (x)))
#endif /* LIMIT_TO_MAX */

/* limit to min */
#ifndef LIMIT_TO_MIN
#define LIMIT_TO_MIN(x, min) \
	(((x) < (min) ? (min) : (x)))
#endif /* LIMIT_TO_MIN */

#define SIZE_BITS(x) (sizeof(x) * NBBY)
#define SIZE_BITS32(x) ((uint)sizeof(x) * NBBY)

#define DELTA(curr, prev) ((curr) > (prev) ? ((curr) - (prev)) : \
	(0xffffffff - (prev) + (curr) + 1))
#define CEIL(x, y)		(((x) + ((y) - 1)) / (y))
#define ROUNDUP(x, y)		((((x) + ((y) - 1)) / (y)) * (y))
#define ROUNDDN(p, align)	((p) & ~((align) - 1))
#define	ISALIGNED(a, x)		(((uintptr)(a) & ((x) - 1)) == 0)
#define ALIGN_ADDR(addr, boundary) (void *)(((uintptr)(addr) + (boundary) - 1) \
	                                         & ~((uintptr)(boundary) - 1))
#define ALIGN_SIZE(size, boundary) (((size) + (boundary) - 1) \
	                                         & ~((boundary) - 1))
#define	ISPOWEROF2(x)		((((x) - 1) & (x)) == 0)
#define VALID_MASK(mask)	!((mask) & ((mask) + 1))

#ifndef OFFSETOF
#if ((__GNUC__ >= 4) && (__GNUC_MINOR__ >= 8))
	/* GCC 4.8+ complains when using our OFFSETOF macro in array length declarations. */
	#define	OFFSETOF(type, member)	__builtin_offsetof(type, member)
#else
#ifdef BCMFUZZ
	/* use 0x10 offset to avoid undefined behavior error due to NULL access */
	#define OFFSETOF(type, member)	(((uint)(uintptr)&((type *)0x10)->member) - 0x10)
#else
	#define	OFFSETOF(type, member)	((uint)(uintptr)&((type *)0)->member)
#endif /* BCMFUZZ */
#endif /* GCC 4.8 or newer */
#endif /* OFFSETOF */

#ifndef CONTAINEROF
#define CONTAINEROF(ptr, type, member) ((type *)((char *)(ptr) - OFFSETOF(type, member)))
#endif /* CONTAINEROF */

/* substruct size up to and including a member of the struct */
#ifndef STRUCT_SIZE_THROUGH
#define STRUCT_SIZE_THROUGH(sptr, fname) \
	(((uint8*)&((sptr)->fname) - (uint8*)(sptr)) + sizeof((sptr)->fname))
#endif

/* Extracting the size of element in a structure */
#define SIZE_OF(type, field) sizeof(((type *)0)->field)

/* Extracting the size of pointer element in a structure */
#define SIZE_OF_PV(type, pfield) sizeof(*((type *)0)->pfield)

#ifndef ARRAYSIZE
#define ARRAYSIZE(a)		(uint32)(sizeof(a) / sizeof(a[0]))
#endif

#ifndef ARRAYLAST /* returns pointer to last array element */
#define ARRAYLAST(a)		(&a[ARRAYSIZE(a)-1])
#endif

/* Calculates the required pad size. This is mainly used in register structures */
#define PADSZ(start, end)       ((((end) - (start)) / 4) + 1)

/* Reference a function; used to prevent a static function from being optimized out */
extern void *_bcmutils_dummy_fn;
#define REFERENCE_FUNCTION(f)	(_bcmutils_dummy_fn = (void *)(f))

/* bit map related macros */
#ifndef setbit
#ifndef NBBY		/* the BSD family defines NBBY */
#define	NBBY	8	/* 8 bits per byte */
#endif /* #ifndef NBBY */
#ifdef BCMUTILS_BIT_MACROS_USE_FUNCS
extern void setbit(void *array, uint bit);
extern void clrbit(void *array, uint bit);
extern bool isset(const void *array, uint bit);
extern bool isclr(const void *array, uint bit);
#else
#define	setbit(a, i)	(((uint8 *)a)[(i) / NBBY] |= 1 << ((i) % NBBY))
#define	clrbit(a, i)	(((uint8 *)a)[(i) / NBBY] &= ~(1 << ((i) % NBBY)))
#define	isset(a, i)	(((const uint8 *)a)[(i) / NBBY] & (1 << ((i) % NBBY)))
#define	isclr(a, i)	((((const uint8 *)a)[(i) / NBBY] & (1 << ((i) % NBBY))) == 0)
#endif
#endif /* setbit */

/* read/write/clear field in a consecutive bits in an octet array.
 * 'addr' is the octet array's start byte address
 * 'size' is the octet array's byte size
 * 'stbit' is the value's start bit offset
 * 'nbits' is the value's bit size
 * This set of utilities are for convenience. Don't use them
 * in time critical/data path as there's a great overhead in them.
 */
void setbits(uint8 *addr, uint size, uint stbit, uint nbits, uint32 val);
uint32 getbits(const uint8 *addr, uint size, uint stbit, uint nbits);
#define clrbits(addr, size, stbit, nbits) setbits(addr, size, stbit, nbits, 0)

extern void set_bitrange(void *array, uint start, uint end, uint maxbit);
extern void clr_bitrange(void *array, uint start, uint end, uint maxbit);
extern void set_bitrange_u32(void *array, uint start, uint end, uint maxbit);
extern void clr_bitrange_u32(void *array, uint start, uint end, uint maxbit);

extern int bcm_find_fsb(uint32 num);

#define	isbitset(a, i)	(((a) & (1 << (i))) != 0)

#if defined DONGLEBUILD
#define	NBITS(type)	(sizeof(type) * 8)
#else
#define	NBITS(type)	((uint32)(sizeof(type) * 8))
#endif  /* DONGLEBUILD */
#define NBITVAL(nbits)	(1 << (nbits))
#define MAXBITVAL(nbits)	((1 << (nbits)) - 1)
#define	NBITMASK(nbits)	MAXBITVAL(nbits)
#define MAXNBVAL(nbyte)	MAXBITVAL((nbyte) * 8)

enum {
	BCM_FMT_BASE32
};
typedef int bcm_format_t;

/* encodes using specified format and returns length of output written on success
 * or a status code BCME_XX on failure. Input and output buffers may overlap.
 * input will be advanced to the position when function stoped.
 * out value of in_len will specify the number of processed input bytes.
 * on input pad_off represents the number of bits (MSBs of the first output byte)
 * to preserve and on output number of pad bits (LSBs) set to 0 in the output.
 */
int bcm_encode(uint8 **in, uint *in_len, bcm_format_t fmt,
		uint *pad_off, uint8 *out, uint out_size);

/* decodes input in specified format, returns length of output written on success
 * or a status code BCME_XX on failure. Input and output buffers may overlap.
 * input will be advanced to the position when function stoped.
 * out value of in_len will specify the number of processed input bytes.
 * on input pad_off represents the number of bits (MSBs of the first output byte)
 * to preserve and on output number of pad bits (LSBs) set to 0 in the output.
 */
int bcm_decode(const uint8 **in, uint *in_len, bcm_format_t fmt,
		uint *pad_off, uint8 *out, uint out_size);

extern void bcm_bitprint32(const uint32 u32);

/*
 * ----------------------------------------------------------------------------
 * Multiword map of 2bits, nibbles
 * setbit2 setbit4 (void *ptr, uint32 ix, uint32 val)
 * getbit2 getbit4 (void *ptr, uint32 ix)
 * ----------------------------------------------------------------------------
 */

#define DECLARE_MAP_API(NB, RSH, LSH, OFF, MSK)                     \
static INLINE void setbit##NB(void *ptr, uint32 ix, uint32 val)     \
{                                                                   \
	uint32 *addr = (uint32 *)ptr;                                   \
	uint32 *a = addr + (ix >> RSH); /* (ix / 2^RSH) */              \
	uint32 pos = (ix & OFF) << LSH; /* (ix % 2^RSH) * 2^LSH */      \
	uint32 mask = (MSK << pos);                                     \
	uint32 tmp = *a & ~mask;                                        \
	*a = tmp | (val << pos);                                        \
}                                                                   \
static INLINE uint32 getbit##NB(void *ptr, uint32 ix)               \
{                                                                   \
	uint32 *addr = (uint32 *)ptr;                                   \
	uint32 *a = addr + (ix >> RSH);                                 \
	uint32 pos = (ix & OFF) << LSH;                                 \
	return ((*a >> pos) & MSK);                                     \
}

DECLARE_MAP_API(2, 4, 1, 15u, 0x0003u) /* setbit2() and getbit2() */
DECLARE_MAP_API(4, 3, 2, 7u, 0x000Fu) /* setbit4() and getbit4() */
DECLARE_MAP_API(8, 2, 3, 3u, 0x00FFu) /* setbit8() and getbit8() */

/* basic mux operation - can be optimized on several architectures */
#define MUX(pred, true, false) ((pred) ? (true) : (false))

/* modulo inc/dec - assumes x E [0, bound - 1] */
#define MODDEC(x, bound) MUX((x) == 0, (bound) - 1, (x) - 1)
#define MODINC(x, bound) MUX((x) == (bound) - 1, 0, (x) + 1)

/* modulo inc/dec, bound = 2^k */
#define MODDEC_POW2(x, bound) (((x) - 1) & ((bound) - 1))
#define MODINC_POW2(x, bound) (((x) + 1) & ((bound) - 1))

/* modulo add/sub - assumes x, y E [0, bound - 1] */
#define MODADD(x, y, bound) \
    MUX((x) + (y) >= (bound), (x) + (y) - (bound), (x) + (y))
#define MODSUB(x, y, bound) \
    MUX(((int)(x)) - ((int)(y)) < 0, (x) - (y) + (bound), (x) - (y))

/* module add/sub, bound = 2^k */
#define MODADD_POW2(x, y, bound) (((x) + (y)) & ((bound) - 1))
#define MODSUB_POW2(x, y, bound) (((x) - (y)) & ((bound) - 1))

/* crc defines */
#define CRC8_INIT_VALUE   0xffu			/* Initial CRC8 checksum value */
#define CRC8_GOOD_VALUE   0x9fu			/* Good final CRC8 checksum value */
#define CRC16_INIT_VALUE  0xffffu		/* Initial CRC16 checksum value */
#define CRC16_GOOD_VALUE  0xf0b8u		/* Good final CRC16 checksum value */
#define CRC32_INIT_VALUE  0xffffffffu		/* Initial CRC32 checksum value */
#define CRC32_GOOD_VALUE  0xdebb20e3u		/* Good final CRC32 checksum value */

#ifdef DONGLEBUILD
#define MACF				"MACADDR:%08x%04x"
#define ETHERP_TO_MACF(ea)		(uint32)bcm_ether_ntou64(ea), \
					(uint32)(bcm_ether_ntou64(ea) >> 32)

#define CONST_ETHERP_TO_MACF(ea)	ETHERP_TO_MACF(ea)

#define ETHER_TO_MACF(ea)		ETHERP_TO_MACF(&ea)

#else
/* use for direct output of MAC address in printf etc */
#define MACF				"%02x:%02x:%02x:%02x:%02x:%02x"
#define ETHERP_TO_MACF(ea)	((const struct ether_addr *) (ea))->octet[0], \
				((const struct ether_addr *) (ea))->octet[1], \
				((const struct ether_addr *) (ea))->octet[2], \
				((const struct ether_addr *) (ea))->octet[3], \
				((const struct ether_addr *) (ea))->octet[4], \
				((const struct ether_addr *) (ea))->octet[5]

#define CONST_ETHERP_TO_MACF(ea)	ETHERP_TO_MACF(ea)

#define ETHER_TO_MACF(ea)	(ea).octet[0], \
				(ea).octet[1], \
				(ea).octet[2], \
				(ea).octet[3], \
				(ea).octet[4], \
				(ea).octet[5]
#endif /* DONGLEBUILD */
/* use only for debug, the string length can be changed
 * If you want to use this macro to the logic,
 * USE MACF instead
 */
#if !defined(SIMPLE_MAC_PRINT)
#define MACDBG "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STRDBG(ea)	((const uint8*)(ea))[0], \
			((const uint8*)(ea))[1], \
			((const uint8*)(ea))[2], \
			((const uint8*)(ea))[3], \
			((const uint8*)(ea))[4], \
			((const uint8*)(ea))[5]
#else
#define MACDBG				"%02x:xx:xx:xx:x%x:%02x"
#define MAC2STRDBG(ea)	((const uint8*)(ea))[0], \
			(((const uint8*)(ea))[4] & 0xf), \
			((const uint8*)(ea))[5]
#endif /* SIMPLE_MAC_PRINT */

#define MACOUIDBG "%02x:%x:%02x"
#define MACOUI2STRDBG(ea)	((const uint8*)(ea))[0], \
				((const uint8*)(ea))[1] & 0xf, \
				((const uint8*)(ea))[2]

#define MACOUI "%02x:%02x:%02x"
#define MACOUI2STR(ea) (ea)[0], (ea)[1], (ea)[2]

/* bcm_format_flags() bit description structure */
typedef struct bcm_bit_desc {
	uint32	bit;
	const char* name;
} bcm_bit_desc_t;

/* bcm_format_field */
typedef struct bcm_bit_desc_ex {
	uint32 mask;
	const bcm_bit_desc_t *bitfield;
} bcm_bit_desc_ex_t;

/* buffer length for ethernet address from bcm_ether_ntoa() */
#define ETHER_ADDR_STR_LEN	18u	/* 18-bytes of Ethernet address buffer length */

static INLINE uint32 /* 32bit word aligned xor-32 */
bcm_compute_xor32(volatile uint32 *u32_val, int num_u32)
{
	int idx;
	uint32 xor32 = 0;
	for (idx = 0; idx < num_u32; idx++)
		xor32 ^= *(u32_val + idx);
	return xor32;
}

/* crypto utility function */
/* 128-bit xor: *dst = *src1 xor *src2. dst1, src1 and src2 may have any alignment */
static INLINE void
xor_128bit_block(const uint8 *src1, const uint8 *src2, uint8 *dst)
{
	if (
#ifdef __i386__
	    1 ||
#endif
	    (((uintptr)src1 | (uintptr)src2 | (uintptr)dst) & 3) == 0) {
		/* ARM CM3 rel time: 1229 (727 if alignment check could be omitted) */
		/* x86 supports unaligned.  This version runs 6x-9x faster on x86. */
		((uint32 *)dst)[0] = ((const uint32 *)src1)[0] ^ ((const uint32 *)src2)[0];
		((uint32 *)dst)[1] = ((const uint32 *)src1)[1] ^ ((const uint32 *)src2)[1];
		((uint32 *)dst)[2] = ((const uint32 *)src1)[2] ^ ((const uint32 *)src2)[2];
		((uint32 *)dst)[3] = ((const uint32 *)src1)[3] ^ ((const uint32 *)src2)[3];
	} else {
		/* ARM CM3 rel time: 4668 (4191 if alignment check could be omitted) */
		int k;
		for (k = 0; k < 16; k++)
			dst[k] = src1[k] ^ src2[k];
	}
}

/* externs */
/* crc */
uint8 hndcrc8(const uint8 *p, uint nbytes, uint8 crc);
uint16 hndcrc16(const uint8 *p, uint nbytes, uint16 crc);
uint32 hndcrc32(const uint8 *p, uint nbytes, uint32 crc);

/* format/print */
/* print out the value a field has: fields may have 1-32 bits and may hold any value */
extern uint bcm_format_field(const bcm_bit_desc_ex_t *bd, uint32 field, char* buf, uint len);
/* print out which bits in flags are set */
extern int bcm_format_flags(const bcm_bit_desc_t *bd, uint32 flags, char* buf, uint len);
/* print out whcih bits in octet array 'addr' are set. bcm_bit_desc_t:bit is a bit offset. */
int bcm_format_octets(const bcm_bit_desc_t *bd, uint bdsz,
	const uint8 *addr, uint size, char *buf, uint len);

extern int bcm_format_hex(char *str, const void *bytes, uint len);

#ifdef BCMDBG
extern void deadbeef(void *p, uint len);
#endif
extern const char *bcm_crypto_algo_name(uint algo);
extern char *bcm_chipname(uint chipid, char *buf, uint len);
extern char *bcm_brev_str(uint32 brev, char *buf);
extern void printbig(char *buf);
extern void prhex(const char *msg, const uchar *buf, uint len);
extern void prhexstr(const char *prefix, const uint8 *buf, uint len, bool newline);

/* bcmerror */
extern const char *bcmerrorstr(int bcmerror);

#if defined(BCMDBG) || defined(WLMSG_ASSOC)
/* get 802.11 frame name based on frame kind - see frame types FC_.. in 802.11.h */
const char *bcm_80211_fk_name(uint fk);
#else
#define bcm_80211_fk_names(_x) ""
#endif

extern int wl_set_up_table(uint8 *up_table, bcm_tlv_t *qos_map_ie);

/* multi-bool data type: set of bools, mbool is true if any is set */
typedef uint32 mbool;
#define mboolset(mb, bit)		((mb) |= (bit))		/* set one bool */
#define mboolclr(mb, bit)		((mb) &= ~(bit))	/* clear one bool */
#define mboolisset(mb, bit)		(((mb) & (bit)) != 0)	/* TRUE if one bool is set */
#define	mboolmaskset(mb, mask, val)	((mb) = (((mb) & ~(mask)) | (val)))

/* generic datastruct to help dump routines */
struct fielddesc {
	const char *nameandfmt;
	uint32 offset;
	uint32 len;
};

extern void bcm_binit(struct bcmstrbuf *b, char *buf, uint size);
#define bcm_bsize(b) ((b)->size)
#define bcm_breset(b) do {bcm_binit(b, (b)->origbuf, (b)->origsize);} while (0)
extern void bcm_bprhex(struct bcmstrbuf *b, const char *msg, bool newline,
	const uint8 *buf, uint len);

extern void bcm_inc_bytes(uchar *num, int num_bytes, uint8 amount);
extern int bcm_cmp_bytes(const uchar *arg1, const uchar *arg2, uint8 nbytes);
extern void bcm_print_bytes(const char *name, const uchar *cdata, uint len);

typedef  uint32 (*bcmutl_rdreg_rtn)(void *arg0, uint arg1, uint32 offset);
extern uint bcmdumpfields(bcmutl_rdreg_rtn func_ptr, void *arg0, uint arg1, struct fielddesc *str,
                          char *buf, uint32 bufsize);
extern uint bcm_bitcount(const uint8 *bitmap, uint bytelength);

extern int bcm_bprintf(struct bcmstrbuf *b, const char *fmt, ...);

/* power conversion */
extern uint16 bcm_qdbm_to_mw(uint8 qdbm);
extern uint8 bcm_mw_to_qdbm(uint16 mw);
extern uint bcm_mkiovar(const char *name, const char *data, uint datalen, char *buf, uint len);

#ifdef BCMDBG_PKT  /* pkt logging for debugging */
#define PKTLIST_SIZE 3000

#ifdef BCMDBG_PTRACE
#define PKTTRACE_MAX_BYTES	12
#define PKTTRACE_MAX_BITS	(PKTTRACE_MAX_BYTES * NBBY)

enum pkttrace_info {
	PKTLIST_PRECQ,		/* Pkt in Prec Q */
	PKTLIST_FAIL_PRECQ, 	/* Pkt failed to Q in PRECQ */
	PKTLIST_DMAQ,		/* Pkt in DMA Q */
	PKTLIST_MI_TFS_RCVD,	/* Received TX status */
	PKTLIST_TXDONE,		/* Pkt TX done */
	PKTLIST_TXFAIL,		/* Pkt TX failed */
	PKTLIST_PKTFREE,	/* pkt is freed */
	PKTLIST_PRECREQ,	/* Pkt requeued in precq */
	PKTLIST_TXFIFO		/* To trace in wlc_fifo */
};
#endif /* BCMDBG_PTRACE */

typedef struct pkt_dbginfo {
	int     line;
	char    *file;
	void	*pkt;
#ifdef BCMDBG_PTRACE
	char	pkt_trace[PKTTRACE_MAX_BYTES];
#endif /* BCMDBG_PTRACE */
} pkt_dbginfo_t;

typedef struct {
	pkt_dbginfo_t list[PKTLIST_SIZE]; /* List of pointers to packets */
	uint16 count; /* Total count of the packets */
} pktlist_info_t;

extern void pktlist_add(pktlist_info_t *pktlist, void *p, int len, char *file);
extern void pktlist_remove(pktlist_info_t *pktlist, void *p);
extern char* pktlist_dump(pktlist_info_t *pktlist, char *buf);
#ifdef BCMDBG_PTRACE
extern void pktlist_trace(pktlist_info_t *pktlist, void *pkt, uint16 bit);
#endif /* BCMDBG_PTRACE */
#endif  /* BCMDBG_PKT */
unsigned int process_nvram_vars(char *varbuf, unsigned int len);
bool replace_nvram_variable(char *varbuf, unsigned int buflen, const char *variable,
	unsigned int *datalen);

/* trace any object allocation / free, with / without features (flags) set to the object */
#if (defined(DONGLEBUILD) && defined(BCMDBG_MEM) && (!defined(BCM_OBJECT_TRACE)))
#define BCM_OBJECT_TRACE
#endif /* (defined(DONGLEBUILD) && defined(BCMDBG_MEM) && (!defined(BCM_OBJECT_TRACE))) */

#define BCM_OBJDBG_ADD           1
#define BCM_OBJDBG_REMOVE        2
#define BCM_OBJDBG_ADD_PKT       3

/* object feature: set or clear flags */
#define BCM_OBJECT_FEATURE_FLAG       1
#define BCM_OBJECT_FEATURE_PKT_STATE  2
/* object feature: flag bits */
#define BCM_OBJECT_FEATURE_0     (1 << 0)
#define BCM_OBJECT_FEATURE_1     (1 << 1)
#define BCM_OBJECT_FEATURE_2     (1 << 2)
/* object feature: clear flag bits field set with this flag */
#define BCM_OBJECT_FEATURE_CLEAR (1 << 31)
#if defined(BCM_OBJECT_TRACE) && !defined(BINCMP)
#define bcm_pkt_validate_chk(obj, func)	do { \
	void * pkttag; \
	bcm_object_trace_chk(obj, 0, 0, \
		func, __LINE__); \
	if ((pkttag = PKTTAG(obj))) { \
		bcm_object_trace_chk(obj, 1, DHD_PKTTAG_SN(pkttag), \
			func, __LINE__); \
	} \
} while (0)
extern void bcm_object_trace_opr(void *obj, uint32 opt, const char *caller, int line);
extern void bcm_object_trace_upd(void *obj, void *obj_new);
extern void bcm_object_trace_chk(void *obj, uint32 chksn, uint32 sn,
	const char *caller, int line);
extern void bcm_object_feature_set(void *obj, uint32 type, uint32 value);
extern int  bcm_object_feature_get(void *obj, uint32 type, uint32 value);
extern void bcm_object_trace_init(void);
extern void bcm_object_trace_deinit(void);
#else
#define bcm_pkt_validate_chk(obj, func)
#define bcm_object_trace_opr(a, b, c, d)
#define bcm_object_trace_upd(a, b)
#define bcm_object_trace_chk(a, b, c, d, e)
#define bcm_object_feature_set(a, b, c)
#define bcm_object_feature_get(a, b, c)
#define bcm_object_trace_init()
#define bcm_object_trace_deinit()
#endif /* BCM_OBJECT_TRACE && !BINCMP */

/* Public domain bit twiddling hacks/utilities: Sean Eron Anderson */

/* Table driven count set bits. */
static const uint8 /* Table only for use by bcm_cntsetbits */
_CSBTBL[256] =
{
	#define B2(n)    n,     n + 1,     n + 1,     n + 2
	#define B4(n) B2(n), B2(n + 1), B2(n + 1), B2(n + 2)
	#define B6(n) B4(n), B4(n + 1), B4(n + 1), B4(n + 2)
		B6(0), B6(0 + 1), B6(0 + 1), B6(0 + 2)
};

static INLINE uint32 /* Uses table _CSBTBL for fast counting of 1's in a u32 */
bcm_cntsetbits(const uint32 u32arg)
{
	/* function local scope declaration of const _CSBTBL[] */
	const uint8 * p = (const uint8 *)&u32arg;
	/* uint32 cast to avoid uint8 being promoted to int for arithmetic operation */
	return ((uint32)_CSBTBL[p[0]] + _CSBTBL[p[1]] + _CSBTBL[p[2]] + _CSBTBL[p[3]]);
}

static INLINE int /* C equivalent count of leading 0's in a u32 */
C_bcm_count_leading_zeros(uint32 u32arg)
{
	int shifts = 0;
	while (u32arg) {
		shifts++; u32arg >>= 1;
	}
	return (32 - shifts);
}

typedef struct bcm_rand_metadata {
	uint32 count;		/* number of random numbers in bytes */
	uint32 signature;	/* host fills it in, FW verfies before reading rand */
} bcm_rand_metadata_t;

#ifdef BCMDRIVER
/*
 * Assembly instructions: Count Leading Zeros
 * "clz"	: MIPS, ARM
 * "cntlzw"	: PowerPC
 * "BSF"	: x86
 * "lzcnt"	: AMD, SPARC
 */

#if defined(__arm__)
#if defined(__ARM_ARCH_7M__) /* Cortex M3 */
#define __USE_ASM_CLZ__
#endif /* __ARM_ARCH_7M__ */
#if defined(__ARM_ARCH_7R__) /* Cortex R4 */
#define __USE_ASM_CLZ__
#endif /* __ARM_ARCH_7R__ */
#endif /* __arm__ */

static INLINE int
bcm_count_leading_zeros(uint32 u32arg)
{
#if defined(__USE_ASM_CLZ__)
	int zeros;
	__asm__ volatile("clz    %0, %1 \n" : "=r" (zeros) : "r"  (u32arg));
	return zeros;
#else	/* C equivalent */
	return C_bcm_count_leading_zeros(u32arg);
#endif  /* C equivalent */
}

/*
 * Macro to count leading zeroes
 *
 */
#if defined(__GNUC__)
#define CLZ(x) __builtin_clzl(x)
#elif defined(__arm__)
#define CLZ(x) __clz(x)
#else
#define CLZ(x) bcm_count_leading_zeros(x)
#endif /* __GNUC__ */

/* INTERFACE: Multiword bitmap based small id allocator. */
struct bcm_mwbmap;	/* forward declaration for use as an opaque mwbmap handle */

#define BCM_MWBMAP_INVALID_HDL	((struct bcm_mwbmap *)NULL)
#define BCM_MWBMAP_INVALID_IDX	((uint32)(~0U))

/* Incarnate a multiword bitmap based small index allocator */
extern struct bcm_mwbmap * bcm_mwbmap_init(osl_t * osh, uint32 items_max);

/* Free up the multiword bitmap index allocator */
extern void bcm_mwbmap_fini(osl_t * osh, struct bcm_mwbmap * mwbmap_hdl);

/* Allocate a unique small index using a multiword bitmap index allocator */
extern uint32 bcm_mwbmap_alloc(struct bcm_mwbmap * mwbmap_hdl);

/* Force an index at a specified position to be in use */
extern void bcm_mwbmap_force(struct bcm_mwbmap * mwbmap_hdl, uint32 bitix);

/* Free a previously allocated index back into the multiword bitmap allocator */
extern void bcm_mwbmap_free(struct bcm_mwbmap * mwbmap_hdl, uint32 bitix);

/* Fetch the toal number of free indices in the multiword bitmap allocator */
extern uint32 bcm_mwbmap_free_cnt(struct bcm_mwbmap * mwbmap_hdl);

/* Determine whether an index is inuse or free */
extern bool bcm_mwbmap_isfree(struct bcm_mwbmap * mwbmap_hdl, uint32 bitix);

/* Debug dump a multiword bitmap allocator */
extern void bcm_mwbmap_show(struct bcm_mwbmap * mwbmap_hdl);

extern void bcm_mwbmap_audit(struct bcm_mwbmap * mwbmap_hdl);
/* End - Multiword bitmap based small Id allocator. */

/* INTERFACE: Simple unique 16bit Id Allocator using a stack implementation. */

#define ID8_INVALID     0xFFu
#define ID16_INVALID    0xFFFFu
#define ID32_INVALID    0xFFFFFFFFu
#define ID16_UNDEFINED              ID16_INVALID

/*
 * Construct a 16bit id allocator, managing 16bit ids in the range:
 *    [start_val16 .. start_val16+total_ids)
 * Note: start_val16 is inclusive.
 * Returns an opaque handle to the 16bit id allocator.
 */
extern void * id16_map_init(osl_t *osh, uint16 total_ids, uint16 start_val16);
extern void * id16_map_fini(osl_t *osh, void * id16_map_hndl);
extern void id16_map_clear(void * id16_map_hndl, uint16 total_ids, uint16 start_val16);

/* Allocate a unique 16bit id */
extern uint16 id16_map_alloc(void * id16_map_hndl);

/* Free a 16bit id value into the id16 allocator */
extern void id16_map_free(void * id16_map_hndl, uint16 val16);

/* Get the number of failures encountered during id allocation. */
extern uint32 id16_map_failures(void * id16_map_hndl);

/* Audit the 16bit id allocator state. */
extern bool id16_map_audit(void * id16_map_hndl);
/* End - Simple 16bit Id Allocator. */
#endif /* BCMDRIVER */

void bcm_add_64(uint32* r_hi, uint32* r_lo, uint32 offset);
void bcm_sub_64(uint32* r_hi, uint32* r_lo, uint32 offset);

#define MASK_32_BITS	(~0)
#define MASK_8_BITS	((1 << 8) - 1)

#define EXTRACT_LOW32(num)	(uint32)(num & MASK_32_BITS)
#define EXTRACT_HIGH32(num)	(uint32)(((uint64)num >> 32) & MASK_32_BITS)

#define MAXIMUM(a, b) ((a > b) ? a : b)
#define MINIMUM(a, b) ((a < b) ? a : b)
#define LIMIT(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

/* calculate checksum for ip header, tcp / udp header / data */
uint16 bcm_ip_cksum(uint8 *buf, uint32 len, uint32 sum);

#ifndef _dll_t_
#define _dll_t_
/*
 * -----------------------------------------------------------------------------
 *                      Double Linked List Macros
 * -----------------------------------------------------------------------------
 *
 * All dll operations must be performed on a pre-initialized node.
 * Inserting an uninitialized node into a list effectively initialized it.
 *
 * When a node is deleted from a list, you may initialize it to avoid corruption
 * incurred by double deletion. You may skip initialization if the node is
 * immediately inserted into another list.
 *
 * By placing a dll_t element at the start of a struct, you may cast a dll_t *
 * to the struct or vice versa.
 *
 * Example of declaring an initializing someList and inserting nodeA, nodeB
 *
 *     typedef struct item {
 *         dll_t node;
 *         int someData;
 *     } Item_t;
 *     Item_t nodeA, nodeB, nodeC;
 *     nodeA.someData = 11111, nodeB.someData = 22222, nodeC.someData = 33333;
 *
 *     dll_t someList;
 *     dll_init(&someList);
 *
 *     dll_append(&someList, (dll_t *) &nodeA);
 *     dll_prepend(&someList, &nodeB.node);
 *     dll_insert((dll_t *)&nodeC, &nodeA.node);
 *
 *     dll_delete((dll_t *) &nodeB);
 *
 * Example of a for loop to walk someList of node_p
 *
 *   extern void mydisplay(Item_t * item_p);
 *
 *   dll_t * item_p, * next_p;
 *   for (item_p = dll_head_p(&someList); ! dll_end(&someList, item_p);
 *        item_p = next_p)
 *   {
 *       next_p = dll_next_p(item_p);
 *       ... use item_p at will, including removing it from list ...
 *       mydisplay((PItem_t)item_p);
 *   }
 *
 * -----------------------------------------------------------------------------
 */
typedef struct dll {
	struct dll * next_p;
	struct dll * prev_p;
} dll_t;

static INLINE void
dll_init(dll_t *node_p)
{
	node_p->next_p = node_p;
	node_p->prev_p = node_p;
}
/* dll macros returing a pointer to dll_t */

static INLINE dll_t *
BCMPOSTTRAPFN(dll_head_p)(dll_t *list_p)
{
	return list_p->next_p;
}

static INLINE dll_t *
BCMPOSTTRAPFN(dll_tail_p)(dll_t *list_p)
{
	return (list_p)->prev_p;
}

static INLINE dll_t *
BCMPOSTTRAPFN(dll_next_p)(dll_t *node_p)
{
	return (node_p)->next_p;
}

static INLINE dll_t *
BCMPOSTTRAPFN(dll_prev_p)(dll_t *node_p)
{
	return (node_p)->prev_p;
}

static INLINE bool
BCMPOSTTRAPFN(dll_empty)(dll_t *list_p)
{
	return ((list_p)->next_p == (list_p));
}

static INLINE bool
BCMPOSTTRAPFN(dll_end)(dll_t *list_p, dll_t * node_p)
{
	return (list_p == node_p);
}

/* inserts the node new_p "after" the node at_p */
static INLINE void
BCMPOSTTRAPFN(dll_insert)(dll_t *new_p, dll_t * at_p)
{
	new_p->next_p = at_p->next_p;
	new_p->prev_p = at_p;
	at_p->next_p = new_p;
	(new_p->next_p)->prev_p = new_p;
}

static INLINE void
BCMPOSTTRAPFN(dll_append)(dll_t *list_p, dll_t *node_p)
{
	dll_insert(node_p, dll_tail_p(list_p));
}

static INLINE void
BCMPOSTTRAPFN(dll_prepend)(dll_t *list_p, dll_t *node_p)
{
	dll_insert(node_p, list_p);
}

/* deletes a node from any list that it "may" be in, if at all. */
static INLINE void
BCMPOSTTRAPFN(dll_delete)(dll_t *node_p)
{
	node_p->prev_p->next_p = node_p->next_p;
	node_p->next_p->prev_p = node_p->prev_p;
}
#endif  /* ! defined(_dll_t_) */

/* Elements managed in a double linked list */

typedef struct dll_pool {
	dll_t       free_list;
	uint16      free_count;
	uint16      elems_max;
	uint16      elem_size;
	dll_t       elements[1];
} dll_pool_t;

dll_pool_t * dll_pool_init(void * osh, uint16 elems_max, uint16 elem_size);
void * dll_pool_alloc(dll_pool_t * dll_pool_p);
void dll_pool_free(dll_pool_t * dll_pool_p, void * elem_p);
void dll_pool_free_tail(dll_pool_t * dll_pool_p, void * elem_p);
typedef void (* dll_elem_dump)(void * elem_p);
#ifdef BCMDBG
void dll_pool_dump(dll_pool_t * dll_pool_p, dll_elem_dump dump);
#endif
void dll_pool_detach(void * osh, dll_pool_t * pool, uint16 elems_max, uint16 elem_size);

int valid_bcmerror(int e);
/* Stringify macro definition */
#define BCM_STRINGIFY(s) #s
/* Used to pass in a macro variable that gets expanded and then stringified */
#define BCM_EXTENDED_STRINGIFY(s) BCM_STRINGIFY(s)

/* calculate IPv4 header checksum
 * - input ip points to IP header in network order
 * - output cksum is in network order
 */
uint16 ipv4_hdr_cksum(uint8 *ip, uint ip_len);

/* calculate IPv4 TCP header checksum
 * - input ip and tcp points to IP and TCP header in network order
 * - output cksum is in network order
 */
uint16 ipv4_tcp_hdr_cksum(uint8 *ip, uint8 *tcp, uint16 tcp_len);

/* calculate IPv6 TCP header checksum
 * - input ipv6 and tcp points to IPv6 and TCP header in network order
 * - output cksum is in network order
 */
uint16 ipv6_tcp_hdr_cksum(uint8 *ipv6, uint8 *tcp, uint16 tcp_len);

#ifdef __cplusplus
	}
#endif

/* #define DEBUG_COUNTER */
#ifdef DEBUG_COUNTER
#define CNTR_TBL_MAX 10
typedef struct _counter_tbl_t {
	char name[16];				/* name of this counter table */
	uint32 prev_log_print;		/* Internal use. Timestamp of the previous log print */
	uint log_print_interval;	/* Desired interval to print logs in ms */
	uint needed_cnt;			/* How many counters need to be used */
	uint32 cnt[CNTR_TBL_MAX];		/* Counting entries to increase at desired places */
	bool enabled;				/* Whether to enable printing log */
} counter_tbl_t;

/*	How to use
	Eg.: In dhd_linux.c
	cnt[0]: How many times dhd_start_xmit() was called in every 1sec.
	cnt[1]: How many bytes were requested to be sent in every 1sec.

++	static counter_tbl_t xmit_tbl = {"xmit", 0, 1000, 2, {0,}, 1};

	int
	dhd_start_xmit(struct sk_buff *skb, struct net_device *net)
	{
		..........
++		counter_printlog(&xmit_tbl);
++		xmit_tbl.cnt[0]++;

		ifp = dhd->iflist[ifidx];
		datalen  = PKTLEN(dhdp->osh, skb);

++		xmit_tbl.cnt[1] += datalen;
		............

		ret = dhd_sendpkt(&dhd->pub, ifidx, pktbuf);
		...........
	}
*/

void counter_printlog(counter_tbl_t *ctr_tbl);
#endif /* DEBUG_COUNTER */

#if defined(__GNUC__)
#define CALL_SITE __builtin_return_address(0)
#elif defined(_WIN32)
#define CALL_SITE _ReturnAddress()
#else
#define CALL_SITE ((void*) 0)
#endif
#ifdef SHOW_LOGTRACE
#define TRACE_LOG_BUF_MAX_SIZE 1700
#define RTT_LOG_BUF_MAX_SIZE 1700
#define BUF_NOT_AVAILABLE	0
#define NEXT_BUF_NOT_AVAIL	1
#define NEXT_BUF_AVAIL		2

typedef struct trace_buf_info {
	int availability;
	int size;
	char buf[TRACE_LOG_BUF_MAX_SIZE];
} trace_buf_info_t;
#endif /* SHOW_LOGTRACE */

enum dump_dongle_e {
	DUMP_DONGLE_COREREG = 0,
	DUMP_DONGLE_D11MEM
};

typedef struct {
	uint32 type;     /**< specifies e.g dump of d11 memory, use enum dump_dongle_e  */
	uint32 index;    /**< iterator1, specifies core index or d11 memory index */
	uint32 offset;   /**< iterator2, byte offset within register set or memory */
} dump_dongle_in_t;

typedef struct {
	uint32 address;  /**< e.g. backplane address of register */
	uint32 id;       /**< id, e.g. core id */
	uint32 rev;      /**< rev, e.g. core rev */
	uint32 n_bytes;  /**< nbytes in array val[] */
	uint32 val[1];   /**< out: values that were read out of registers or memory */
} dump_dongle_out_t;

extern uint32 sqrt_int(uint32 value);

extern uint8 bcm_get_ceil_pow_2(uint val);

#ifdef BCMDRIVER
/* structures and routines to process variable sized data */
typedef struct var_len_data {
	uint32	vlen;
	uint8	*vdata;
} var_len_data_t;

int bcm_vdata_alloc(osl_t *osh, var_len_data_t *vld, uint32 size);
int bcm_vdata_free(osl_t *osh, var_len_data_t *vld);
#if defined(PRIVACY_MASK)
void bcm_ether_privacy_mask(struct ether_addr *addr);
#else
#define bcm_ether_privacy_mask(addr)
#endif /* PRIVACY_MASK */
#endif /* BCMDRIVER */

/* Count the number of elements in an array that do not match the given value */
extern int array_value_mismatch_count(uint8 value, uint8 *array, int array_size);
/* Count the number of non-zero elements in an uint8 array */
extern int array_nonzero_count(uint8 *array, int array_size);
/* Count the number of non-zero elements in an int16 array */
extern int array_nonzero_count_int16(int16 *array, int array_size);
/* Count the number of zero elements in an uint8 array */
extern int array_zero_count(uint8 *array, int array_size);
/* Validate a uint8 ordered array.  Assert if invalid. */
extern int verify_ordered_array_uint8(uint8 *array, int array_size, uint8 range_lo, uint8 range_hi);
/* Validate a int16 configuration array that need not be zero-terminated.  Assert if invalid. */
extern int verify_ordered_array_int16(int16 *array, int array_size, int16 range_lo, int16 range_hi);
/* Validate all values in an array are in range */
extern int verify_array_values(uint8 *array, int array_size,
	int range_lo, int range_hi, bool zero_terminated);

/*  To unwind from the trap_handler. */
extern void (*const print_btrace_int_fn)(int depth, uint32 pc, uint32 lr, uint32 sp);
extern void (*const print_btrace_fn)(int depth);
#define PRINT_BACKTRACE(depth) if (print_btrace_fn) print_btrace_fn(depth)
#define PRINT_BACKTRACE_INT(depth, pc, lr, sp) \
	if (print_btrace_int_fn) print_btrace_int_fn(depth, pc, lr, sp)

/* FW Signing - only in bootloader builds, never in dongle FW builds */
#ifdef WL_FWSIGN
	#define FWSIGN_ENAB()		(1)
#else
	#define FWSIGN_ENAB()		(0)
#endif /* WL_FWSIGN */

/* Utilities for reading SROM/SFlash vars */

typedef struct varbuf {
	char *base;		/* pointer to buffer base */
	char *buf;		/* pointer to current position */
	unsigned int size;	/* current (residual) size in bytes */
} varbuf_t;

/** Initialization of varbuf structure */
void varbuf_init(varbuf_t *b, char *buf, uint size);
/** append a null terminated var=value string */
int varbuf_append(varbuf_t *b, const char *fmt, ...);
#if defined(BCMDRIVER)
int initvars_table(osl_t *osh, char *start, char *end, char **vars, uint *count);
#endif

/* Count the number of trailing zeros in uint32 val
 * Applying unary minus to unsigned value is intentional,
 * and doesn't influence counting of trailing zeros
 */
static INLINE uint32
count_trailing_zeros(uint32 val)
{
#ifdef BCMDRIVER
	uint32 c = (uint32)CLZ(val & ((uint32)(-(int)val)));
#else
	uint32 c = (uint32)C_bcm_count_leading_zeros(val & ((uint32)(-(int)val)));
#endif /* BCMDRIVER */
	return val ? 31u - c : c;
}

/** Size in bytes of data block, defined by struct with last field, declared as
 * one/zero element vector - such as wl_uint32_list_t or bcm_xtlv_cbuf_s.
 * Arguments:
 * list - address of data block (value is ignored, only type is important)
 * last_var_len_field - name of last field (usually declared as ...[] or ...[1])
 * num_elems - number of elements in data block
 * Example:
 * wl_uint32_list_t *list;
 * WL_VAR_LEN_STRUCT_SIZE(list, element, 10);  // Size in bytes of 10-element list
 */
#define WL_VAR_LEN_STRUCT_SIZE(list, last_var_len_field, num_elems) \
	((size_t)((const char *)&((list)->last_var_len_field) - (const char *)(list)) + \
	(sizeof((list)->last_var_len_field[0]) * (size_t)(num_elems)))

int buf_shift_right(uint8 *buf, uint16 len, uint8 bits);
#endif	/* _bcmutils_h_ */
