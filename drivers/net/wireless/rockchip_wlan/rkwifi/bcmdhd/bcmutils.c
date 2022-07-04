/*
 * Driver O/S-independent utility routines
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

#include <typedefs.h>
#include <bcmdefs.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0))
#include <stdarg.h>
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) */
#ifdef BCMDRIVER
#include <osl.h>
#include <bcmutils.h>
#if !defined(BCMDONGLEHOST) || defined(BCMNVRAM)
#include <bcmnvram.h>
#endif

#else /* !BCMDRIVER */

#include <stdio.h>
#include <string.h>
#include <bcmutils.h>

#if defined(BCMEXTSUP)
#include <bcm_osl.h>
#endif

#ifndef ASSERT
#define ASSERT(exp)
#endif

#endif /* !BCMDRIVER */

#ifdef WL_UNITTEST
/*
 * Definitions and includes needed during software unit test compilation and execution.
 */
#include <stdio.h>
#include <stdlib.h>
#ifdef ASSERT
#undef ASSERT
#endif /* ASSERT */
#define ASSERT(exp)
#endif /* WL_UNITTEST */

#if defined(_WIN32) || defined(NDIS)
/* Debatable */
#include <bcmstdlib.h>
#endif
#include <bcmstdlib_s.h>
#include <bcmendian.h>
#include <bcmdevs.h>
#include <ethernet.h>
#include <vlan.h>
#include <bcmip.h>
#include <802.1d.h>
#include <802.11.h>
#include <bcmip.h>
#include <bcmipv6.h>
#include <bcmtcp.h>
#ifdef BCMPERFSTATS
#include <bcmperf.h>
#endif
#include <wl_android.h>

#define NUMBER_OF_BITS_BYTE	8u

#ifdef CUSTOM_DSCP_TO_PRIO_MAPPING
#define CUST_IPV4_TOS_PREC_MASK 0x3F
#define DCSP_MAX_VALUE 64
extern uint dhd_dscpmap_enable;
/* 0:BE,1:BK,2:RESV(BK):,3:EE,:4:CL,5:VI,6:VO,7:NC */
int dscp2priomap[DCSP_MAX_VALUE]=
{
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, /* BK->BE */
	2, 0, 0, 0, 0, 0, 0, 0,
	3, 0, 0, 0, 0, 0, 0, 0,
	4, 0, 0, 0, 0, 0, 0, 0,
	5, 0, 0, 0, 0, 0, 0, 0,
	6, 0, 0, 0, 0, 0, 0, 0,
	7, 0, 0, 0, 0, 0, 0, 0
};
#endif /* CUSTOM_DSCP_TO_PRIO_MAPPING */

#ifdef PRIVACY_MASK
struct ether_addr privacy_addrmask;

/* RAM accessor function to avoid 'privacy_addrmask' in ROM/RAM shared data section. */
static struct ether_addr *
BCMRAMFN(privacy_addrmask_get)(void)
{
	return &privacy_addrmask;
}
#endif /* PRIVACY_MASK */

#ifdef BCMDRIVER

#ifndef BCM_ARM_BACKTRACE
/* function pointers for firmware stack backtrace utility */
void (*const print_btrace_int_fn)(int depth, uint32 pc, uint32 lr, uint32 sp) = NULL;
void (*const print_btrace_fn)(int depth) = NULL;
#else
void print_backtrace(int depth);
void (*const print_btrace_fn)(int depth) = print_backtrace;
void print_backtrace_int(int depth, uint32 pc, uint32 lr, uint32 sp);
void (*const print_btrace_int_fn)(int depth, uint32 pc, uint32 lr, uint32 sp) = print_backtrace_int;
#endif

#if !defined(BCMDONGLEHOST)
/* Forward declarations */
char * getvar_internal(char *vars, const char *name);
int getintvar_internal(char *vars, const char *name);
int getintvararray_internal(char *vars, const char *name, int index);
int getintvararraysize_internal(char *vars, const char *name);

#ifndef WL_FWSIGN
/*
 * Search the name=value vars for a specific one and return its value.
 * Returns NULL if not found.
 */
char *
getvar(char *vars, const char *name)
{
	NVRAM_RECLAIM_CHECK(name);
	return getvar_internal(vars, name);
}

char *
getvar_internal(char *vars, const char *name)
{
	char *s;
	uint len;

	if (!name)
		return NULL;

	len = strlen(name);
	if (len == 0u) {
		return NULL;
	}

	/* first look in vars[] */
	for (s = vars; s && *s;) {
		if ((bcmp(s, name, len) == 0) && (s[len] == '=')) {
			return (&s[len+1u]);
		}
		while (*s++)
			;
	}

	/* then query nvram */
	return (nvram_get(name));
}

/*
 * Search the vars for a specific one and return its value as
 * an integer. Returns 0 if not found.
 */
int
getintvar(char *vars, const char *name)
{
	NVRAM_RECLAIM_CHECK(name);
	return getintvar_internal(vars, name);
}

int
getintvar_internal(char *vars, const char *name)
{
	char *val;

	if ((val = getvar_internal(vars, name)) == NULL)
		return (0);

	return (bcm_strtoul(val, NULL, 0));
}

int
getintvararray(char *vars, const char *name, int index)
{
	NVRAM_RECLAIM_CHECK(name);
	return getintvararray_internal(vars, name, index);
}

int
getintvararray_internal(char *vars, const char *name, int index)
{
	char *buf, *endp;
	int i = 0;
	int val = 0;

	if ((buf = getvar_internal(vars, name)) == NULL) {
		return (0);
	}

	/* table values are always separated by "," or " " */
	while (*buf != '\0') {
		val = bcm_strtoul(buf, &endp, 0);
		if (i == index) {
			return val;
		}
		buf = endp;
		/* delimiter is ',' */
		if (*buf == ',')
			buf++;
		i++;
	}
	return (0);
}

int
getintvararraysize(char *vars, const char *name)
{
	NVRAM_RECLAIM_CHECK(name);
	return getintvararraysize_internal(vars, name);
}

int
getintvararraysize_internal(char *vars, const char *name)
{
	char *buf, *endp;
	int count = 0;
	int val = 0;

	if ((buf = getvar_internal(vars, name)) == NULL) {
		return (0);
	}

	/* table values are always separated by "," or " " */
	while (*buf != '\0') {
		val = bcm_strtoul(buf, &endp, 0);
		buf = endp;
		/* delimiter is ',' */
		if (*buf == ',')
			buf++;
		count++;
	}
	BCM_REFERENCE(val);
	return count;
}

/* Read an array of values from a possibly slice-specific nvram string
 * Store the values in either the uint8 dest_array1 or in the int16 dest_array2.
 * Pass in NULL for the dest_array[12] that is not to be used.
 */
static int
BCMATTACHFN(getintvararray_slicespecific)(osl_t *osh, char *vars, char *vars_table_accessor,
	const char* name, uint8* dest_array1, int16* dest_array2, uint dest_size)
{
	uint i;
	uint array_size = 0;
	int err = BCME_OK;
	uint prefixed_name_sz;
	char *prefixed_name = NULL;
	const char *new_name;
	int val;

	prefixed_name_sz = get_slicespecific_var_name(osh, vars_table_accessor,
		name, &prefixed_name);
	if (prefixed_name_sz == 0) {
		return BCME_NOMEM;
	}

	new_name = prefixed_name;
	(void) new_name;
	if (getvar(vars, new_name) == NULL) {
		/* Try again without the slice prefix in the name */
		new_name = name;
		if (getvar(vars, name) == NULL) {
			err = BCME_NOTFOUND;
			goto done;
		}
	}

	array_size = (uint)getintvararraysize(vars, new_name);
	if (array_size > dest_size) {
		err = BCME_BUFTOOSHORT;
		ASSERT(array_size <= dest_size);
		goto done;
	}

	/* limit the initialization to the size of the nvram array */
	array_size = MIN(array_size, dest_size);

	/* load the destination array with the nvram array values */
	for (i = 0; i < array_size; i++) {
		val = getintvararray(vars, new_name, i);
		if (dest_array1) {
			dest_array1[i] = (uint8)val;
		} else if (dest_array2) {
			dest_array2[i] = (int16)val;
		}
	}
done:
	MFREE(osh, prefixed_name, prefixed_name_sz);
	return (err < 0) ? err : (int)array_size;
}

int
BCMATTACHFN(get_uint8_vararray_slicespecific)(osl_t *osh, char *vars, char *vars_table_accessor,
	const char* name, uint8* dest_array, uint dest_size)
{
	int ret;

	ret = getintvararray_slicespecific(osh, vars, vars_table_accessor,
		name, dest_array, NULL, dest_size);
	return ret;
}

int
BCMATTACHFN(get_int16_vararray_slicespecific)(osl_t *osh, char *vars, char *vars_table_accessor,
	const char* name, int16* dest_array, uint dest_size)
{
	return getintvararray_slicespecific(osh, vars, vars_table_accessor,
		name, NULL, dest_array, dest_size);
}

/* Prepend a slice-specific accessor to an nvram string name.
 * Sets name_out to the allocated string. Returns the allocated size of the name string.
 * Caller is responsible for freeing the resulting name string with MFREE.
 */
uint
BCMATTACHFN(get_slicespecific_var_name)(osl_t *osh, char *vars_table_accessor, const char *name,
	char **name_out)
{
	char *name_with_prefix = NULL;
	uint sz;
	uint max_copy_size;

	sz = strlen(name) + strlen(vars_table_accessor) + 1;
	name_with_prefix = (char *) MALLOC_NOPERSIST(osh, sz);
	if (name_with_prefix == NULL) {
		sz = 0;
		goto end;
	}
	name_with_prefix[0] = 0;
	name_with_prefix[sz - 1] = 0;
	max_copy_size = sz - 1;

	/* if accessor contains a "slice/N/" string */
	if (vars_table_accessor[0] != 0) {
		/* prepend accessor to the vars-name */
		bcmstrncat(name_with_prefix, vars_table_accessor, max_copy_size);
		max_copy_size -= strlen(name_with_prefix);
	}

	/* Append vars-name */
	bcmstrncat(name_with_prefix, name, max_copy_size);
end:
	*name_out = name_with_prefix;
	return sz;
}
#endif /* WL_FWSIGN */

/* Search for token in comma separated token-string */
static int
findmatch(const char *string, const char *name)
{
	uint len;
	char *c;

	len = strlen(name);
	while ((c = strchr(string, ',')) != NULL) {
		if (len == (uint)(c - string) && !strncmp(string, name, len))
			return 1;
		string = c + 1;
	}

	return (!strcmp(string, name));
}

/* Return gpio pin number assigned to the named pin
 *
 * Variable should be in format:
 *
 *	gpio<N>=pin_name,pin_name
 *
 * This format allows multiple features to share the gpio with mutual
 * understanding.
 *
 * 'def_pin' is returned if a specific gpio is not defined for the requested functionality
 * and if def_pin is not used by others.
 */
uint
getgpiopin(char *vars, char *pin_name, uint def_pin)
{
	char name[] = "gpioXXXX";
	char *val;
	uint pin;

	/* Go thru all possibilities till a match in pin name */
	for (pin = 0; pin < GPIO_NUMPINS; pin ++) {
		snprintf(name, sizeof(name), "gpio%d", pin);
		val = getvar(vars, name);
		if (val && findmatch(val, pin_name))
			return pin;
	}

	if (def_pin != GPIO_PIN_NOTDEFINED) {
		/* make sure the default pin is not used by someone else */
		snprintf(name, sizeof(name), "gpio%d", def_pin);
		if (getvar(vars, name)) {
			def_pin =  GPIO_PIN_NOTDEFINED;
		}
	}
	return def_pin;
}
#endif /* !BCMDONGLEHOST */

/* return total length of buffer chain. In case of CSO, submsdu may have extra tsohdr and if
 * pktotlen should not include submsdu tso header, use the API pkttotlen_no_sfhtoe_hdr.
 */
uint
BCMFASTPATH(pkttotlen)(osl_t *osh, void *p)
{
	uint total = 0;

	for (; p; p = PKTNEXT(osh, p)) {
		total += PKTLEN(osh, p);

		if (BCMLFRAG_ENAB() && PKTISFRAG(osh, p)) {
			total += PKTFRAGTOTLEN(osh, p);
		}
	}

	return (total);
}

#ifdef WLCSO
/* return total length of buffer chain, but exclude tso hdr of submsdu if its added  */
uint
BCMFASTPATH(pkttotlen_no_sfhtoe_hdr)(osl_t *osh, void *p, uint toe_hdr_len)
{
	uint total = 0;

	for (; p; p = PKTNEXT(osh, p)) {
		total += PKTLEN(osh, p);

		/* exclude toe_hdr_len if its part of PKTLEN()  */
		if (PKTISSUBMSDUTOEHDR(osh, p)) {
			total -= toe_hdr_len;
		}

		if (BCMLFRAG_ENAB() && PKTISFRAG(osh, p)) {
			total += PKTFRAGTOTLEN(osh, p);
		}
	}

	return (total);
}
#endif /* WLCSO */

/* return total length of buffer chain */
uint
BCMFASTPATH(pkttotcnt)(osl_t *osh, void *p)
{
	uint cnt = 0;

	for (; p; p = PKTNEXT(osh, p)) {
		cnt++;
	}

	return (cnt);
}

/* return the last buffer of chained pkt */
void *
BCMFASTPATH(pktlast)(osl_t *osh, void *p)
{
	for (; PKTNEXT(osh, p); p = PKTNEXT(osh, p))
		;

	return (p);
}

/* count segments of a chained packet */
uint
BCMFASTPATH(pktsegcnt)(osl_t *osh, void *p)
{
	uint cnt;

	for (cnt = 0; p; p = PKTNEXT(osh, p)) {
		if (PKTLEN(osh, p)) {
			cnt++;
		}
#ifdef BCMLFRAG
		if (BCMLFRAG_ENAB() && PKTISFRAG(osh, p)) {
				cnt += PKTFRAGTOTNUM(osh, p);
		}
#endif /* BCMLFRAG */
	}

	return cnt;
}

#ifdef DONGLEBUILD
/**
 * Takes in a lbuf/lfrag and no of bytes to be trimmed from tail.
 * trim bytes  could be spread out in below 3 formats
 *     1. entirely in dongle
 *     2. entirely in host
 *     3. split between host-dongle
 */
void
BCMFASTPATH(pktfrag_trim_tailbytes)(osl_t * osh, void* p, uint16 trim_len, uint8 type)
{
	uint16 tcmseg_len = PKTLEN(osh, p);	/* TCM segment length */
	uint16 hostseg_len = PKTFRAGUSEDLEN(osh, p);	/* HOST segment length */

	/* return if zero trim length- Nothing to do */
	if (trim_len == 0)
		return;

	/* if header conv is on, there is no fcs at the end */
	/* JIRA:SW4349-318 */
	if (PKTISHDRCONVTD(osh, p))
		return;

	/* if pktfetched, then its already trimmed */
	if (PKTISPKTFETCHED(osh, p))
		return;

	if (PKTFRAGUSEDLEN(osh, p) >= trim_len) {
		/* TRIM bytes entirely in host */
		ASSERT_FP(PKTISRXFRAG(osh, p));

		PKTSETFRAGUSEDLEN(osh, p, (hostseg_len - trim_len));
	} else {
		/* trim bytes either in dongle or split between dongle-host */
		PKTSETLEN(osh, p, (tcmseg_len - (trim_len - hostseg_len)));

		/* No more contents in host; reset length to zero */
		if (PKTFRAGUSEDLEN(osh, p))
			PKTSETFRAGUSEDLEN(osh, p, 0);
	}
}
#endif /* DONGLEBUILD */

/* copy a pkt buffer chain into a buffer */
uint
pktcopy(osl_t *osh, void *p, uint offset, uint len, uchar *buf)
{
	uint n, ret = 0;

	/* skip 'offset' bytes */
	for (; p && offset; p = PKTNEXT(osh, p)) {
		if (offset < PKTLEN(osh, p))
			break;
		offset -= PKTLEN(osh, p);
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = PKTNEXT(osh, p)) {
		n = MIN(PKTLEN(osh, p) - offset, len);
		bcopy(PKTDATA(osh, p) + offset, buf, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}

/* copy a buffer into a pkt buffer chain */
uint
pktfrombuf(osl_t *osh, void *p, uint offset, uint len, uchar *buf)
{
	uint n, ret = 0;

	/* skip 'offset' bytes */
	for (; p && offset; p = PKTNEXT(osh, p)) {
		if (offset < PKTLEN(osh, p))
			break;
		offset -= PKTLEN(osh, p);
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = PKTNEXT(osh, p)) {
		n = MIN(PKTLEN(osh, p) - offset, len);
		bcopy(buf, PKTDATA(osh, p) + offset, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}

#ifdef NOT_YET
/* copy data from one pkt buffer (chain) to another */
uint
pkt2pktcopy(osl_t *osh, void *p1, uint offs1, void *p2, uint offs2, uint maxlen)
{
	uint8 *dp1, *dp2;
	uint len1, len2, copylen, totallen;

	for (; p1 && offs; p1 = PKTNEXT(osh, p1)) {
		if (offs1 < (uint)PKTLEN(osh, p1))
			break;
		offs1 -= PKTLEN(osh, p1);
	}
	for (; p2 && offs; p2 = PKTNEXT(osh, p2)) {
		if (offs2 < (uint)PKTLEN(osh, p2))
			break;
		offs2 -= PKTLEN(osh, p2);
	}

	/* Heck w/it, only need the above for now */
}
#endif /* NOT_YET */

uint8 *
BCMFASTPATH(pktdataoffset)(osl_t *osh, void *p,  uint offset)
{
	uint total = pkttotlen(osh, p);
	uint pkt_off = 0, len = 0;
	uint8 *pdata = (uint8 *) PKTDATA(osh, p);

	if (offset > total)
		return NULL;

	for (; p; p = PKTNEXT(osh, p)) {
		pdata = (uint8 *) PKTDATA(osh, p);
		pkt_off = offset - len;
		len += PKTLEN(osh, p);
		if (len > offset)
			break;
	}
	return (uint8*) (pdata+pkt_off);
}

/* given a offset in pdata, find the pkt seg hdr */
void *
pktoffset(osl_t *osh, void *p,  uint offset)
{
	uint total = pkttotlen(osh, p);
	uint len = 0;

	if (offset > total)
		return NULL;

	for (; p; p = PKTNEXT(osh, p)) {
		len += PKTLEN(osh, p);
		if (len > offset)
			break;
	}
	return p;
}

void
bcm_mdelay(uint ms)
{
	uint i;

	for (i = 0; i < ms; i++) {
		OSL_DELAY(1000);
	}
}

#if defined(BCMPERFSTATS) || defined(BCMTSTAMPEDLOGS)

#if defined(__ARM_ARCH_7R__)
#define BCMLOG_CYCLE_OVERHEAD	54	/* Number of CPU cycle overhead due to bcmlog().
					 * This is to compensate CPU cycle incurred by
					 * added bcmlog() function call for profiling.
					 */
#else
#define BCMLOG_CYCLE_OVERHEAD	0
#endif

#define	LOGSIZE	256			/* should be power of 2 to avoid div below */
static struct {
	uint	cycles;
	const char	*fmt;
	uint	a1;
	uint	a2;
	uchar   indent;		/* track indent level for nice printing */
} logtab[LOGSIZE];

/* last entry logged  */
static uint logi = 0;
/* next entry to read */
static uint volatile readi = 0;
#endif	/* defined(BCMPERFSTATS) || defined(BCMTSTAMPEDLOGS) */

#ifdef BCMPERFSTATS
/* TODO: make the utility configurable (choose between icache, dcache, hits, misses ...)  */
void
bcm_perf_enable()
{
	BCMPERF_ENABLE_INSTRCOUNT();
	BCMPERF_ENABLE_ICACHE_MISS();
	BCMPERF_ENABLE_ICACHE_HIT();
}

/* WARNING:  This routine uses OSL_GETCYCLES(), which can give unexpected results on
 * modern speed stepping CPUs.  Use bcmtslog() instead in combination with TSF counter.
 */
void
bcmlog(char *fmt, uint a1, uint a2)
{
	static uint last = 0;
	uint cycles, i, elapsed;
	OSL_GETCYCLES(cycles);

	i = logi;

	elapsed = cycles - last;
	if (elapsed > BCMLOG_CYCLE_OVERHEAD)
		logtab[i].cycles = elapsed - BCMLOG_CYCLE_OVERHEAD;
	else
		logtab[i].cycles = 0;
	logtab[i].fmt = fmt;
	logtab[i].a1 = a1;
	logtab[i].a2 = a2;

	logi = (i + 1) % LOGSIZE;
	last = cycles;

	/* if log buffer is overflowing, readi should be advanced.
	 * Otherwise logi and readi will become out of sync.
	 */
	if (logi == readi) {
		readi = (readi + 1) % LOGSIZE;
	} else {
		/* This redundant else is to make CPU cycles of bcmlog() function to be uniform,
		 * so that the cycle compensation with BCMLOG_CYCLE_OVERHEAD is more accurate.
		 */
		readi = readi % LOGSIZE;
	}
}

/* Same as bcmlog but specializes the use of a1 and a2 to
 * store icache misses and instruction count.
 *  TODO : make this use a configuration array to decide what counter to read.
 * We are limited to 2 numbers but it seems it is the most we can get anyway
 * since dcache and icache cannot be enabled at the same time. Recording
 * both the hits and misses at the same time for a given cache is not that useful either.
*/

void
bcmstats(char *fmt)
{
	static uint last = 0;
	static uint32 ic_miss = 0;
	static uint32 instr_count = 0;
	uint32 ic_miss_cur;
	uint32 instr_count_cur;
	uint cycles, i;

	OSL_GETCYCLES(cycles);
	BCMPERF_GETICACHE_MISS(ic_miss_cur);
	BCMPERF_GETINSTRCOUNT(instr_count_cur);

	i = logi;

	logtab[i].cycles = cycles - last;
	logtab[i].a1 = ic_miss_cur - ic_miss;
	logtab[i].a2 = instr_count_cur - instr_count;
	logtab[i].fmt = fmt;

	logi = (i + 1) % LOGSIZE;

	last = cycles;
	instr_count = instr_count_cur;
	ic_miss = ic_miss_cur;

	/* if log buffer is overflowing, readi should be advanced.
	 * Otherwise logi and readi will become out of sync.
	 */
	if (logi == readi) {
		readi = (readi + 1) % LOGSIZE;
	} else {
		/* This redundant else is to make CPU cycles of bcmstats() function to be uniform
		 */
		readi = readi % LOGSIZE;
	}
}

/*
 * TODO (linux version): a "proc" version where the log would be dumped
 * on the proc file directly.
 */

void
bcmdumplog(char *buf, int size)
{
	char *limit;
	int j = 0;
	int num;

	limit = buf + size - 80;
	*buf = '\0';

	num = logi - readi;

	if (num < 0)
		num += LOGSIZE;

	/* print in chronological order */

	for (j = 0; j < num && (buf < limit); readi = (readi + 1) % LOGSIZE, j++) {
		if (logtab[readi].fmt == NULL)
		    continue;
		buf += snprintf(buf, (limit - buf), "%d\t", logtab[readi].cycles);
		buf += snprintf(buf, (limit - buf), logtab[readi].fmt, logtab[readi].a1,
			logtab[readi].a2);
		buf += snprintf(buf, (limit - buf), "\n");
	}

}

/*
 * Dump one log entry at a time.
 * Return index of next entry or -1 when no more .
 */
int
bcmdumplogent(char *buf, uint i)
{
	bool hit;

	/*
	 * If buf is NULL, return the starting index,
	 * interpreting i as the indicator of last 'i' entries to dump.
	 */
	if (buf == NULL) {
		i = ((i > 0) && (i < (LOGSIZE - 1))) ? i : (LOGSIZE - 1);
		return ((logi - i) % LOGSIZE);
	}

	*buf = '\0';

	ASSERT(i < LOGSIZE);

	if (i == logi)
		return (-1);

	hit = FALSE;
	for (; (i != logi) && !hit; i = (i + 1) % LOGSIZE) {
		if (logtab[i].fmt == NULL)
			continue;
		buf += snprintf(buf, LOGSIZE, "%d: %d\t", i, logtab[i].cycles);
		buf += snprintf(buf, LOGSIZE, logtab[i].fmt, logtab[i].a1, logtab[i].a2);
		buf += snprintf(buf, LOGSIZE, "\n");
		hit = TRUE;
	}

	return (i);
}

#endif	/* BCMPERFSTATS */

#if defined(BCMTSTAMPEDLOGS)
/* Store a TSF timestamp and a log line in the log buffer */
/*
	a1 is used to signify entering/exiting a routine.  When entering
	the indent level is increased.  When exiting, the delta since entering
	is printed and the indent level is bumped back out.
	Nesting can go up to level MAX_TS_INDENTS deep.
*/
#define MAX_TS_INDENTS 20
void
bcmtslog(uint32 tstamp, const char *fmt, uint a1, uint a2)
{
	uint i = logi;
	bool use_delta = TRUE;
	static uint32 last = 0;	/* used only when use_delta is true */
	static uchar indent = 0;
	static uint32 indents[MAX_TS_INDENTS];

	logtab[i].cycles = tstamp;
	if (use_delta)
		logtab[i].cycles -= last;

	logtab[i].a2 = a2;

	if (a1 == TS_EXIT && indent) {
		indent--;
		logtab[i].a2 = tstamp - indents[indent];
	}

	logtab[i].fmt = fmt;
	logtab[i].a1 = a1;
	logtab[i].indent = indent;

	if (a1 == TS_ENTER) {
		indents[indent] = tstamp;
		if (indent < MAX_TS_INDENTS - 1)
			indent++;
	}

	if (use_delta)
		last = tstamp;
	logi = (i + 1) % LOGSIZE;
}

/* Print out a microsecond timestamp as "sec.ms.us " */
void
bcmprinttstamp(uint32 ticks)
{
	uint us, ms, sec;

	us = (ticks % TSF_TICKS_PER_MS) * 1000 / TSF_TICKS_PER_MS;
	ms = ticks / TSF_TICKS_PER_MS;
	sec = ms / 1000;
	ms -= sec * 1000;
	printf("%04u.%03u.%03u ", sec, ms, us);
}

/* Print out the log buffer with timestamps */
void
bcmprinttslogs(void)
{
	int j = 0;
	int num;

	num = logi - readi;
	if (num < 0)
		num += LOGSIZE;

	/* Format and print the log entries directly in chronological order */
	for (j = 0; j < num; readi = (readi + 1) % LOGSIZE, j++) {
		if (logtab[readi].fmt == NULL)
		    continue;
		bcmprinttstamp(logtab[readi].cycles);
		printf(logtab[readi].fmt, logtab[readi].a1, logtab[readi].a2);
		printf("\n");
	}
}

/*
	Identical to bcmdumplog, but output is based on tsf instead of cycles.

	a1 is used to signify entering/exiting a routine.  When entering
	the indent level is increased.  When exiting, the delta since entering
	is printed and the indent level is bumped back out.
*/
void
bcmdumptslog(struct bcmstrbuf *b)
{
	char *limit;
	int j = 0;
	int num;
	uint us, ms, sec;
	int skip;
	char *lines = "| | | | | | | | | | | | | | | | | | | |";

	limit = BCMSTRBUF_BUF(b) + BCMSTRBUF_LEN(b) - 80;

	num = logi - readi;

	if (num < 0)
		num += LOGSIZE;

	/* print in chronological order */
	for (j = 0; j < num && (BCMSTRBUF_BUF(b) < limit); readi = (readi + 1) % LOGSIZE, j++) {
		char *last_buf = BCMSTRBUF_BUF(b);
		if (logtab[readi].fmt == NULL)
			continue;

		us = (logtab[readi].cycles % TSF_TICKS_PER_MS) * 1000 / TSF_TICKS_PER_MS;
		ms = logtab[readi].cycles / TSF_TICKS_PER_MS;
		sec = ms / 1000;
		ms -= sec * 1000;

		bcm_bprintf(b, "%04u.%03u.%03u ", sec, ms, us);

		/* 2 spaces for each indent level */
		bcm_bprintf(b, "%.*s", logtab[readi].indent * 2, lines);

		/*
		 * The following call to snprintf generates a compiler warning
		 * due to -Wformat-security. However, the format string is coming
		 * from internal callers rather than external data input, and is a
		 * useful debugging tool serving a variety of diagnostics. Rather
		 * than expand code size by replicating multiple functions with different
		 * argument lists, or disabling the warning globally, let's consider
		 * if we can just disable the warning for this one instance.
		 */
		bcm_bprintf(b, logtab[readi].fmt);

		/* If a1 is ENTER or EXIT, print the + or - */
		skip = 0;
		if (logtab[readi].a1 == TS_ENTER) {
			bcm_bprintf(b, " +");
			skip++;
		}
		if (logtab[readi].a1 == TS_EXIT) {
			bcm_bprintf(b, " -");
			skip++;
		}

		/* else print the real a1 */
		if (logtab[readi].a1 && !skip)
			bcm_bprintf(b, "   %d", logtab[readi].a1);

		/*
		   If exiting routine, print a nicely formatted delta since entering.
		   Otherwise, just print a2 normally.
		*/
		if (logtab[readi].a2) {
			if (logtab[readi].a1 == TS_EXIT) {
				int num_space = 75 - (BCMSTRBUF_BUF(b) - last_buf);
				bcm_bprintf(b, "%*.s", num_space, "");
				bcm_bprintf(b, "%5d usecs", logtab[readi].a2);
			} else
				bcm_bprintf(b, "  %d", logtab[readi].a2);
		}
		bcm_bprintf(b, "\n");
		last_buf = BCMSTRBUF_BUF(b);
	}
}

#endif	/* BCMTSTAMPEDLOGS */

#if defined(BCMDBG) || defined(DHD_DEBUG)
/* pretty hex print a pkt buffer chain */
void
prpkt(const char *msg, osl_t *osh, void *p0)
{
	void *p;

	if (msg && (msg[0] != '\0'))
		printf("%s:\n", msg);

	for (p = p0; p; p = PKTNEXT(osh, p))
		prhex(NULL, PKTDATA(osh, p), PKTLEN(osh, p));
}
#endif	/* BCMDBG || DHD_DEBUG */

/* Takes an Ethernet frame and sets out-of-bound PKTPRIO.
 * Also updates the inplace vlan tag if requested.
 * For debugging, it returns an indication of what it did.
 */
uint
BCMFASTPATH(pktsetprio)(void *pkt, bool update_vtag)
{
	struct ether_header *eh;
	struct ethervlan_header *evh;
	uint8 *pktdata;
	int priority = 0;
	int rc = 0;

	pktdata = (uint8 *)PKTDATA(OSH_NULL, pkt);
	ASSERT_FP(ISALIGNED((uintptr)pktdata, sizeof(uint16)));

	eh = (struct ether_header *) pktdata;

	if (eh->ether_type == hton16(ETHER_TYPE_8021Q)) {
		uint16 vlan_tag;
		int vlan_prio, dscp_prio = 0;

		evh = (struct ethervlan_header *)eh;

		vlan_tag = ntoh16(evh->vlan_tag);
		vlan_prio = (int) (vlan_tag >> VLAN_PRI_SHIFT) & VLAN_PRI_MASK;

		if ((evh->ether_type == hton16(ETHER_TYPE_IP)) ||
			(evh->ether_type == hton16(ETHER_TYPE_IPV6))) {
			uint8 *ip_body = pktdata + sizeof(struct ethervlan_header);
			uint8 tos_tc = IP_TOS46(ip_body);
			dscp_prio = (int)(tos_tc >> IPV4_TOS_PREC_SHIFT);
		}

		/* DSCP priority gets precedence over 802.1P (vlan tag) */
		if (dscp_prio != 0) {
			priority = dscp_prio;
			rc |= PKTPRIO_VDSCP;
		} else {
			priority = vlan_prio;
			rc |= PKTPRIO_VLAN;
		}
		/*
		 * If the DSCP priority is not the same as the VLAN priority,
		 * then overwrite the priority field in the vlan tag, with the
		 * DSCP priority value. This is required for Linux APs because
		 * the VLAN driver on Linux, overwrites the skb->priority field
		 * with the priority value in the vlan tag
		 */
		if (update_vtag && (priority != vlan_prio)) {
			vlan_tag &= ~(VLAN_PRI_MASK << VLAN_PRI_SHIFT);
			vlan_tag |= (uint16)priority << VLAN_PRI_SHIFT;
			evh->vlan_tag = hton16(vlan_tag);
			rc |= PKTPRIO_UPD;
		}
#if defined(EAPOL_PKT_PRIO) || defined(DHD_LOSSLESS_ROAMING)
	} else if (eh->ether_type == hton16(ETHER_TYPE_802_1X)) {
		priority = PRIO_8021D_NC;
		rc = PKTPRIO_DSCP;
#endif /* EAPOL_PKT_PRIO || DHD_LOSSLESS_ROAMING */
#if defined(WLTDLS)
	} else if (eh->ether_type == hton16(ETHER_TYPE_89_0D)) {
		/* Bump up the priority for TDLS frames */
		priority = PRIO_8021D_VI;
		rc = PKTPRIO_DSCP;
#endif /* WLTDLS */
	} else if ((eh->ether_type == hton16(ETHER_TYPE_IP)) ||
		(eh->ether_type == hton16(ETHER_TYPE_IPV6))) {
		uint8 *ip_body = pktdata + sizeof(struct ether_header);
		uint8 tos_tc = IP_TOS46(ip_body);
		uint8 dscp = tos_tc >> IPV4_TOS_DSCP_SHIFT;
		switch (dscp) {
		case DSCP_EF:
		case DSCP_VA:
			priority = PRIO_8021D_VO;
			break;
		case DSCP_AF31:
		case DSCP_AF32:
		case DSCP_AF33:
		case DSCP_CS3:
			priority = PRIO_8021D_CL;
			break;
		case DSCP_AF21:
		case DSCP_AF22:
		case DSCP_AF23:
			priority = PRIO_8021D_EE;
			break;
		case DSCP_AF11:
		case DSCP_AF12:
		case DSCP_AF13:
		case DSCP_CS2:
			priority = PRIO_8021D_BE;
			break;
		case DSCP_CS6:
		case DSCP_CS7:
			priority = PRIO_8021D_NC;
			break;
		default:
#ifndef CUSTOM_DSCP_TO_PRIO_MAPPING
			priority = (int)(tos_tc >> IPV4_TOS_PREC_SHIFT);
#else
			if (dhd_dscpmap_enable) {
				priority = (int)dscp2priomap[((tos_tc >> IPV4_TOS_DSCP_SHIFT)
					& CUST_IPV4_TOS_PREC_MASK)];
			}
			else {
				priority = (int)(tos_tc >> IPV4_TOS_PREC_SHIFT);
			}
#endif /* CUSTOM_DSCP_TO_PRIO_MAPPING */
			break;
		}

		rc |= PKTPRIO_DSCP;
	}

	ASSERT_FP(priority >= 0 && priority <= MAXPRIO);
	PKTSETPRIO(pkt, priority);
	return (rc | priority);
}

/* lookup user priority for specified DSCP */
static uint8
dscp2up(uint8 *up_table, uint8 dscp)
{
	uint8 user_priority = 255;

	/* lookup up from table if parameters valid */
	if (up_table != NULL && dscp < UP_TABLE_MAX) {
		user_priority = up_table[dscp];
	}

	/* 255 is unused value so return up from dscp */
	if (user_priority == 255) {
		user_priority = dscp >> (IPV4_TOS_PREC_SHIFT - IPV4_TOS_DSCP_SHIFT);
	}

	return user_priority;
}

/* set user priority by QoS Map Set table (UP table), table size is UP_TABLE_MAX */
uint
BCMFASTPATH(pktsetprio_qms)(void *pkt, uint8* up_table, bool update_vtag)
{
	if (up_table) {
		uint8 *pktdata;
		uint pktlen;
		uint8 dscp;
		uint user_priority = 0;
		uint rc = 0;

		pktdata = (uint8 *)PKTDATA(OSH_NULL, pkt);
		pktlen = PKTLEN(OSH_NULL, pkt);
		if (pktgetdscp(pktdata, pktlen, &dscp)) {
			rc = PKTPRIO_DSCP;
			user_priority = dscp2up(up_table, dscp);
			PKTSETPRIO(pkt, user_priority);
		}

		return (rc | user_priority);
	} else {
		return pktsetprio(pkt, update_vtag);
	}
}

/* Returns TRUE and DSCP if IP header found, FALSE otherwise.
 */
bool
BCMFASTPATH(pktgetdscp)(uint8 *pktdata, uint pktlen, uint8 *dscp)
{
	struct ether_header *eh;
	struct ethervlan_header *evh;
	uint8 *ip_body;
	bool rc = FALSE;

	/* minimum length is ether header and IP header */
	if (pktlen < (sizeof(struct ether_header) + IPV4_MIN_HEADER_LEN)) {
		return FALSE;
	}

	eh = (struct ether_header *) pktdata;

	if ((eh->ether_type == HTON16(ETHER_TYPE_IP)) ||
		(eh->ether_type == HTON16(ETHER_TYPE_IPV6))) {
		ip_body = pktdata + sizeof(struct ether_header);
		*dscp = IP_DSCP46(ip_body);
		rc = TRUE;
	}
	else if (eh->ether_type == HTON16(ETHER_TYPE_8021Q)) {
		evh = (struct ethervlan_header *)eh;

		/* minimum length is ethervlan header and IP header */
		if (pktlen >= sizeof(struct ethervlan_header) + IPV4_MIN_HEADER_LEN &&
			evh->ether_type == HTON16(ETHER_TYPE_IP)) {
			ip_body = pktdata + sizeof(struct ethervlan_header);
			*dscp = IP_DSCP46(ip_body);
			rc = TRUE;
		}
	}

	return rc;
}

/* usr_prio range from low to high with usr_prio value */
static bool
up_table_set(uint8 *up_table, uint8 usr_prio, uint8 low, uint8 high)
{
	int i;

	if (usr_prio > 7 || low > high || low >= UP_TABLE_MAX || high >= UP_TABLE_MAX) {
		return FALSE;
	}

	for (i = low; i <= high; i++) {
		up_table[i] = usr_prio;
	}

	return TRUE;
}

/* set user priority table */
int
BCMFASTPATH(wl_set_up_table)(uint8 *up_table, bcm_tlv_t *qos_map_ie)
{
	uint8 len;

	if (up_table == NULL || qos_map_ie == NULL) {
		return BCME_ERROR;
	}

	/* clear table to check table was set or not */
	memset(up_table, 0xff, UP_TABLE_MAX);

	/* length of QoS Map IE must be 16+n*2, n is number of exceptions */
	if (qos_map_ie != NULL && qos_map_ie->id == DOT11_MNG_QOS_MAP_ID &&
			(len = qos_map_ie->len) >= QOS_MAP_FIXED_LENGTH &&
			(len % 2) == 0) {
		uint8 *except_ptr = (uint8 *)qos_map_ie->data;
		uint8 except_len = len - QOS_MAP_FIXED_LENGTH;
		uint8 *range_ptr = except_ptr + except_len;
		int i;

		/* fill in ranges */
		for (i = 0; i < QOS_MAP_FIXED_LENGTH; i += 2) {
			uint8 low = range_ptr[i];
			uint8 high = range_ptr[i + 1];
			if (low == 255 && high == 255) {
				continue;
			}

			if (!up_table_set(up_table, i / 2, low, high)) {
				/* clear the table on failure */
				memset(up_table, 0xff, UP_TABLE_MAX);
				return BCME_ERROR;
			}
		}

		/* update exceptions */
		for (i = 0; i < except_len; i += 2) {
			uint8 dscp = except_ptr[i];
			uint8 usr_prio = except_ptr[i+1];

			/* exceptions with invalid dscp/usr_prio are ignored */
			up_table_set(up_table, usr_prio, dscp, dscp);
		}
	}

	return BCME_OK;
}

#ifndef BCM_BOOTLOADER
/* The 0.5KB string table is not removed by compiler even though it's unused */

static char bcm_undeferrstr[32];
static const char *bcmerrorstrtable[] = BCMERRSTRINGTABLE;

/* Convert the error codes into related error strings  */
/* BCMRAMFN for BCME_LAST usage */
const char *
BCMRAMFN(bcmerrorstr)(int bcmerror)
{
	/* check if someone added a bcmerror code but forgot to add errorstring */
	ASSERT(ABS(BCME_LAST) == (ARRAYSIZE(bcmerrorstrtable) - 1));

	if (bcmerror > 0 || bcmerror < BCME_LAST) {
		snprintf(bcm_undeferrstr, sizeof(bcm_undeferrstr), "Undefined error %d", bcmerror);
		return bcm_undeferrstr;
	}

	ASSERT(strlen(bcmerrorstrtable[-bcmerror]) < BCME_STRLEN);

	return bcmerrorstrtable[-bcmerror];
}

#endif /* !BCM_BOOTLOADER */

#ifdef BCMDBG_PKT   /* pkt logging for debugging */
/* Add a packet to the pktlist */
static void
_pktlist_add(pktlist_info_t *pktlist, void *pkt, int line, char *file)
{
	uint16 i;
	char *basename;
#ifdef BCMDBG_PTRACE
	uint16 *idx = PKTLIST_IDX(pkt);
#endif /* BCMDBG_PTRACE */

	ASSERT(pktlist->count < PKTLIST_SIZE);

	/* Verify the packet is not already part of the list */
	for (i = 0; i < pktlist->count; i++) {
		if (pktlist->list[i].pkt == pkt)
			ASSERT(0);
	}
	pktlist->list[pktlist->count].pkt = pkt;
	pktlist->list[pktlist->count].line = line;

	basename = strrchr(file, '/');
	if (basename)
		basename++;
	else
		basename = file;
	pktlist->list[pktlist->count].file = basename;
#ifdef BCMDBG_PTRACE
	*idx = pktlist->count;
	bzero(pktlist->list[pktlist->count].pkt_trace, PKTTRACE_MAX_BYTES);
#endif /* BCMDBG_PTRACE */
	pktlist->count++;

	return;
}

void
pktlist_add(pktlist_info_t *pktlist, void *pkt, int line, char *file)
{
	void *p;
	for (p = pkt; p != NULL; p = PKTCLINK(p))
		_pktlist_add(pktlist, p, line, file);
}

/* Remove a packet from the pktlist */
static void
_pktlist_remove(pktlist_info_t *pktlist, void *pkt)
{
	uint16 i;
	uint16 num = pktlist->count;
#ifdef BCMDBG_PTRACE
	uint16 *idx = PKTLIST_IDX(pkt);

	ASSERT((*idx) < pktlist->count);
#endif /* BCMDBG_PTRACE */

	/* find the index where pkt exists */
	for (i = 0; i < num; i++) {
		/* check for the existence of pkt in the list */
		if (pktlist->list[i].pkt == pkt) {
#ifdef BCMDBG_PTRACE
			ASSERT((*idx) == i);
#endif /* BCMDBG_PTRACE */
			/* replace with the last element */
			pktlist->list[i].pkt = pktlist->list[num-1].pkt;
			pktlist->list[i].line = pktlist->list[num-1].line;
			pktlist->list[i].file = pktlist->list[num-1].file;
#ifdef BCMDBG_PTRACE
			memcpy(pktlist->list[i].pkt_trace, pktlist->list[num-1].pkt_trace,
				PKTTRACE_MAX_BYTES);
			idx = PKTLIST_IDX(pktlist->list[i].pkt);
			*idx = i;
#endif /* BCMDBG_PTRACE */
			pktlist->count--;
			return;
		}
	}
	ASSERT(0);
}

void
pktlist_remove(pktlist_info_t *pktlist, void *pkt)
{
	void *p;
	for (p = pkt; p != NULL; p = PKTCLINK(p))
		_pktlist_remove(pktlist, p);
}

#ifdef BCMDBG_PTRACE
static void
_pktlist_trace(pktlist_info_t *pktlist, void *pkt, uint16 bit)
{
	uint16 *idx = PKTLIST_IDX(pkt);

	ASSERT(((*idx) < pktlist->count) && (bit < PKTTRACE_MAX_BITS));
	ASSERT(pktlist->list[(*idx)].pkt == pkt);

	pktlist->list[(*idx)].pkt_trace[bit/NBBY] |= (1 << ((bit)%NBBY));

}
void
pktlist_trace(pktlist_info_t *pktlist, void *pkt, uint16 bit)
{
	void *p;
	for (p = pkt; p != NULL; p = PKTCLINK(p))
		_pktlist_trace(pktlist, p, bit);
}
#endif /* BCMDBG_PTRACE */

/* Dump the pktlist (and the contents of each packet if 'data'
 * is set). 'buf' should be large enough
 */

char *
pktlist_dump(pktlist_info_t *pktlist, char *buf)
{
	char *obuf = buf;
	uint16 i;

	if (buf != NULL)
		buf += sprintf(buf, "Packet list dump:\n");
	else
		printf("Packet list dump:\n");

	for (i = 0; i < (pktlist->count); i++) {
		if (buf != NULL)
			buf += sprintf(buf, "Pkt_addr: 0x%p Line: %d File: %s\t",
				OSL_OBFUSCATE_BUF(pktlist->list[i].pkt), pktlist->list[i].line,
				pktlist->list[i].file);
		else
			printf("Pkt_addr: 0x%p Line: %d File: %s\t",
				OSL_OBFUSCATE_BUF(pktlist->list[i].pkt),
				pktlist->list[i].line, pktlist->list[i].file);

/* #ifdef NOTDEF  Remove this ifdef to print pkttag and pktdata */
		if (buf != NULL) {
			if (PKTTAG(pktlist->list[i].pkt)) {
				/* Print pkttag */
				buf += sprintf(buf, "Pkttag(in hex): ");
				buf += bcm_format_hex(buf, PKTTAG(pktlist->list[i].pkt),
					OSL_PKTTAG_SZ);
			}
			buf += sprintf(buf, "Pktdata(in hex): ");
			buf += bcm_format_hex(buf, PKTDATA(OSH_NULL, pktlist->list[i].pkt),
			                      PKTLEN(OSH_NULL, pktlist->list[i].pkt));
		} else {
			void *pkt = pktlist->list[i].pkt, *npkt;

			printf("Pkt[%d] Dump:\n", i);
			while (pkt) {
				int hroom;
				uint pktlen;
				uchar *src;
#ifdef BCMDBG_PTRACE
				uint16 *idx = PKTLIST_IDX(pkt);

				ASSERT((*idx) < pktlist->count);
				prhex("Pkt Trace (in hex):", pktlist->list[(*idx)].pkt_trace,
					PKTTRACE_MAX_BYTES);
#endif /* BCMDBG_PTRACE */
				npkt = (void *)PKTNEXT(OSH_NULL, pkt);
				PKTSETNEXT(OSH_NULL, pkt, NULL);

				src = (uchar *)(PKTTAG(pkt));
				pktlen = PKTLEN(OSH_NULL, pkt);
				hroom = PKTHEADROOM(OSH_NULL, pkt);

				printf("Pkttag_addr: %p\n", OSL_OBFUSCATE_BUF(src));
				if (src)
					prhex("Pkttag(in hex): ", src, OSL_PKTTAG_SZ);
				src = (uchar *) (PKTDATA(OSH_NULL, pkt));
				printf("Pkthead_addr: %p len: %d\n",
					OSL_OBFUSCATE_BUF(src - hroom), hroom);
				prhex("Pkt headroom content(in hex): ", src - hroom, hroom);
				printf("Pktdata_addr: %p len: %d\n",
					OSL_OBFUSCATE_BUF(src), pktlen);
				prhex("Pktdata(in hex): ", src, pktlen);

				pkt = npkt;
			}
		}
/* #endif  NOTDEF */

		if (buf != NULL)
			buf += sprintf(buf, "\n");
		else
			printf("\n");
	}
	return obuf;
}
#endif  /* BCMDBG_PKT */

/* iovar table lookup */
/* could mandate sorted tables and do a binary search */
const bcm_iovar_t*
bcm_iovar_lookup(const bcm_iovar_t *table, const char *name)
{
	const bcm_iovar_t *vi;
	const char *lookup_name;

	/* skip any ':' delimited option prefixes */
	lookup_name = strrchr(name, ':');
	if (lookup_name != NULL)
		lookup_name++;
	else
		lookup_name = name;

	ASSERT(table != NULL);

	for (vi = table; vi->name; vi++) {
		if (!strcmp(vi->name, lookup_name))
			return vi;
	}
	/* ran to end of table */

	return NULL; /* var name not found */
}

int
bcm_iovar_lencheck(const bcm_iovar_t *vi, void *arg, uint len, bool set)
{
	int bcmerror = 0;
	BCM_REFERENCE(arg);

	/* length check on io buf */
	switch (vi->type) {
	case IOVT_BOOL:
	case IOVT_INT8:
	case IOVT_INT16:
	case IOVT_INT32:
	case IOVT_UINT8:
	case IOVT_UINT16:
	case IOVT_UINT32:
		/* all integers are int32 sized args at the ioctl interface */
		if (len < sizeof(int)) {
			bcmerror = BCME_BUFTOOSHORT;
		}
		break;

	case IOVT_BUFFER:
		/* buffer must meet minimum length requirement */
		if (len < vi->minlen) {
			bcmerror = BCME_BUFTOOSHORT;
		}
		break;

	case IOVT_VOID:
		if (!set) {
			/* Cannot return nil... */
			bcmerror = BCME_UNSUPPORTED;
		}
		break;

	default:
		/* unknown type for length check in iovar info */
		ASSERT(0);
		bcmerror = BCME_UNSUPPORTED;
	}

	return bcmerror;
}

/*
 * Hierarchical Multiword bitmap based small id allocator.
 *
 * Multilevel hierarchy bitmap. (maximum 2 levels)
 * First hierarchy uses a multiword bitmap to identify 32bit words in the
 * second hierarchy that have at least a single bit set. Each bit in a word of
 * the second hierarchy represents a unique ID that may be allocated.
 *
 * BCM_MWBMAP_ITEMS_MAX: Maximum number of IDs managed.
 * BCM_MWBMAP_BITS_WORD: Number of bits in a bitmap word word
 * BCM_MWBMAP_WORDS_MAX: Maximum number of bitmap words needed for free IDs.
 * BCM_MWBMAP_WDMAP_MAX: Maximum number of bitmap wordss identifying first non
 *                       non-zero bitmap word carrying at least one free ID.
 * BCM_MWBMAP_SHIFT_OP:  Used in MOD, DIV and MUL operations.
 * BCM_MWBMAP_INVALID_IDX: Value ~0U is treated as an invalid ID
 *
 * Design Notes:
 * BCM_MWBMAP_USE_CNTSETBITS trades CPU for memory. A runtime count of how many
 * bits are computed each time on allocation and deallocation, requiring 4
 * array indexed access and 3 arithmetic operations. When not defined, a runtime
 * count of set bits state is maintained. Upto 32 Bytes per 1024 IDs is needed.
 * In a 4K max ID allocator, up to 128Bytes are hence used per instantiation.
 * In a memory limited system e.g. dongle builds, a CPU for memory tradeoff may
 * be used by defining BCM_MWBMAP_USE_CNTSETBITS.
 *
 * Note: wd_bitmap[] is statically declared and is not ROM friendly ... array
 * size is fixed. No intention to support larger than 4K indice allocation. ID
 * allocators for ranges smaller than 4K will have a wastage of only 12Bytes
 * with savings in not having to use an indirect access, had it been dynamically
 * allocated.
 */
#if defined(DONGLEBUILD)
#define BCM_MWBMAP_USE_CNTSETBITS		/* runtime count set bits */
#if defined(PCIEDEV_HOST_PKTID_AUDIT_ENABLED)
#define BCM_MWBMAP_ITEMS_MAX	(38 * 1024)
#else /* ! PCIEDEV_HOST_PKTID_AUDIT_ENABLED */
#define BCM_MWBMAP_ITEMS_MAX	(7 * 1024)
#endif /* PCIEDEV_HOST_PKTID_AUDIT_ENABLED */
#else  /* ! DONGLEBUILD */
#define BCM_MWBMAP_ITEMS_MAX    (64 * 1024)  /* May increase to 64K */
#endif /*   DONGLEBUILD */

#define BCM_MWBMAP_BITS_WORD    (NBITS(uint32))
#define BCM_MWBMAP_WORDS_MAX    (BCM_MWBMAP_ITEMS_MAX / BCM_MWBMAP_BITS_WORD)
#define BCM_MWBMAP_WDMAP_MAX    (BCM_MWBMAP_WORDS_MAX / BCM_MWBMAP_BITS_WORD)
#define BCM_MWBMAP_SHIFT_OP     (5)
#define BCM_MWBMAP_MODOP(ix)    ((ix) & (BCM_MWBMAP_BITS_WORD - 1))
#define BCM_MWBMAP_DIVOP(ix)    ((ix) >> BCM_MWBMAP_SHIFT_OP)
#define BCM_MWBMAP_MULOP(ix)    ((ix) << BCM_MWBMAP_SHIFT_OP)

/* Redefine PTR() and/or HDL() conversion to invoke audit for debugging */
#define BCM_MWBMAP_PTR(hdl)		((struct bcm_mwbmap *)(hdl))
#define BCM_MWBMAP_HDL(ptr)		((void *)(ptr))

#if defined(BCM_MWBMAP_DEBUG)
#define BCM_MWBMAP_AUDIT(mwb) \
	do { \
		ASSERT((mwb != NULL) && \
		       (((struct bcm_mwbmap *)(mwb))->magic == (void *)(mwb))); \
		bcm_mwbmap_audit(mwb); \
	} while (0)
#define MWBMAP_ASSERT(exp)		ASSERT(exp)
#define MWBMAP_DBG(x)           printf x
#else   /* !BCM_MWBMAP_DEBUG */
#define BCM_MWBMAP_AUDIT(mwb)   do {} while (0)
#define MWBMAP_ASSERT(exp)		do {} while (0)
#define MWBMAP_DBG(x)
#endif  /* !BCM_MWBMAP_DEBUG */

typedef struct bcm_mwbmap {     /* Hierarchical multiword bitmap allocator    */
	uint16 wmaps;               /* Total number of words in free wd bitmap    */
	uint16 imaps;               /* Total number of words in free id bitmap    */
	int32  ifree;               /* Count of free indices. Used only in audits */
	uint16 total;               /* Total indices managed by multiword bitmap  */

	void * magic;               /* Audit handle parameter from user           */

	uint32 wd_bitmap[BCM_MWBMAP_WDMAP_MAX]; /* 1st level bitmap of            */
#if !defined(BCM_MWBMAP_USE_CNTSETBITS)
	int8   wd_count[BCM_MWBMAP_WORDS_MAX];  /* free id running count, 1st lvl */
#endif /*  ! BCM_MWBMAP_USE_CNTSETBITS */

	uint32 id_bitmap[0];        /* Second level bitmap                        */
} bcm_mwbmap_t;

/* Incarnate a hierarchical multiword bitmap based small index allocator. */
struct bcm_mwbmap *
BCMATTACHFN(bcm_mwbmap_init)(osl_t *osh, uint32 items_max)
{
	struct bcm_mwbmap * mwbmap_p;
	uint32 wordix, size, words, extra;

	/* Implementation Constraint: Uses 32bit word bitmap */
	MWBMAP_ASSERT(BCM_MWBMAP_BITS_WORD == 32U);
	MWBMAP_ASSERT(BCM_MWBMAP_SHIFT_OP == 5U);
	MWBMAP_ASSERT(ISPOWEROF2(BCM_MWBMAP_ITEMS_MAX));
	MWBMAP_ASSERT((BCM_MWBMAP_ITEMS_MAX % BCM_MWBMAP_BITS_WORD) == 0U);

	ASSERT(items_max <= BCM_MWBMAP_ITEMS_MAX);

	/* Determine the number of words needed in the multiword bitmap */
	extra = BCM_MWBMAP_MODOP(items_max);
	words = BCM_MWBMAP_DIVOP(items_max) + ((extra != 0U) ? 1U : 0U);

	/* Allocate runtime state of multiword bitmap */
	/* Note: wd_count[] or wd_bitmap[] are not dynamically allocated */
	size = sizeof(bcm_mwbmap_t) + (sizeof(uint32) * words);
	mwbmap_p = (bcm_mwbmap_t *)MALLOC(osh, size);
	if (mwbmap_p == (bcm_mwbmap_t *)NULL) {
		ASSERT(0);
		goto error1;
	}
	memset(mwbmap_p, 0, size);

	/* Initialize runtime multiword bitmap state */
	mwbmap_p->imaps = (uint16)words;
	mwbmap_p->ifree = (int32)items_max;
	mwbmap_p->total = (uint16)items_max;

	/* Setup magic, for use in audit of handle */
	mwbmap_p->magic = BCM_MWBMAP_HDL(mwbmap_p);

	/* Setup the second level bitmap of free indices */
	/* Mark all indices as available */
	for (wordix = 0U; wordix < mwbmap_p->imaps; wordix++) {
		mwbmap_p->id_bitmap[wordix] = (uint32)(~0U);
#if !defined(BCM_MWBMAP_USE_CNTSETBITS)
		mwbmap_p->wd_count[wordix] = BCM_MWBMAP_BITS_WORD;
#endif /*  ! BCM_MWBMAP_USE_CNTSETBITS */
	}

	/* Ensure that extra indices are tagged as un-available */
	if (extra) { /* fixup the free ids in last bitmap and wd_count */
		uint32 * bmap_p = &mwbmap_p->id_bitmap[mwbmap_p->imaps - 1];
		*bmap_p ^= (uint32)(~0U << extra); /* fixup bitmap */
#if !defined(BCM_MWBMAP_USE_CNTSETBITS)
		mwbmap_p->wd_count[mwbmap_p->imaps - 1] = (int8)extra; /* fixup count */
#endif /*  ! BCM_MWBMAP_USE_CNTSETBITS */
	}

	/* Setup the first level bitmap hierarchy */
	extra = BCM_MWBMAP_MODOP(mwbmap_p->imaps);
	words = BCM_MWBMAP_DIVOP(mwbmap_p->imaps) + ((extra != 0U) ? 1U : 0U);

	mwbmap_p->wmaps = (uint16)words;

	for (wordix = 0U; wordix < mwbmap_p->wmaps; wordix++)
		mwbmap_p->wd_bitmap[wordix] = (uint32)(~0U);
	if (extra) {
		uint32 * bmap_p = &mwbmap_p->wd_bitmap[mwbmap_p->wmaps - 1];
		*bmap_p ^= (uint32)(~0U << extra); /* fixup bitmap */
	}

	return mwbmap_p;

error1:
	return BCM_MWBMAP_INVALID_HDL;
}

/* Release resources used by multiword bitmap based small index allocator. */
void
BCMATTACHFN(bcm_mwbmap_fini)(osl_t * osh, struct bcm_mwbmap * mwbmap_hdl)
{
	bcm_mwbmap_t * mwbmap_p;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	MFREE(osh, mwbmap_p, sizeof(struct bcm_mwbmap)
			     + (sizeof(uint32) * mwbmap_p->imaps));
	return;
}

/* Allocate a unique small index using a multiword bitmap index allocator.    */
uint32
BCMFASTPATH(bcm_mwbmap_alloc)(struct bcm_mwbmap * mwbmap_hdl)
{
	bcm_mwbmap_t * mwbmap_p;
	uint32 wordix, bitmap;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	/* Start with the first hierarchy */
	for (wordix = 0; wordix < mwbmap_p->wmaps; ++wordix) {

		bitmap = mwbmap_p->wd_bitmap[wordix]; /* get the word bitmap */

		if (bitmap != 0U) {

			uint32 count, bitix, *bitmap_p;

			bitmap_p = &mwbmap_p->wd_bitmap[wordix];

			/* clear all except trailing 1 */
			if (bitmap != (1u << 31u)) {
				bitmap = (uint32)(((int)(bitmap)) & (-((int)(bitmap))));
			}
			MWBMAP_ASSERT(C_bcm_count_leading_zeros(bitmap) ==
			              bcm_count_leading_zeros(bitmap));
			bitix    = (BCM_MWBMAP_BITS_WORD - 1)
				 - bcm_count_leading_zeros(bitmap); /* use asm clz */
			wordix   = BCM_MWBMAP_MULOP(wordix) + bitix;

			/* Clear bit if wd count is 0, without conditional branch */
#if defined(BCM_MWBMAP_USE_CNTSETBITS)
			count = bcm_cntsetbits(mwbmap_p->id_bitmap[wordix]) - 1;
#else  /* ! BCM_MWBMAP_USE_CNTSETBITS */
			mwbmap_p->wd_count[wordix]--;
			count = mwbmap_p->wd_count[wordix];
			MWBMAP_ASSERT(count ==
			              (bcm_cntsetbits(mwbmap_p->id_bitmap[wordix]) - 1));
#endif /* ! BCM_MWBMAP_USE_CNTSETBITS */
			MWBMAP_ASSERT(count >= 0);

			/* clear wd_bitmap bit if id_map count is 0 */
			bitmap = (count == 0) << bitix;

			MWBMAP_DBG((
			    "Lvl1: bitix<%02u> wordix<%02u>: %08x ^ %08x = %08x wfree %d",
			    bitix, wordix, *bitmap_p, bitmap, (*bitmap_p) ^ bitmap, count));

			*bitmap_p ^= bitmap;

			/* Use bitix in the second hierarchy */
			bitmap_p = &mwbmap_p->id_bitmap[wordix];

			bitmap = mwbmap_p->id_bitmap[wordix]; /* get the id bitmap */
			MWBMAP_ASSERT(bitmap != 0U);

			/* clear all except trailing 1 */
			if (bitmap != (1u << 31u)) {
				bitmap = (uint32)(((int)(bitmap)) & (-((int)(bitmap))));
			}
			MWBMAP_ASSERT(C_bcm_count_leading_zeros(bitmap) ==
			              bcm_count_leading_zeros(bitmap));
			bitix    = BCM_MWBMAP_MULOP(wordix)
				 + (BCM_MWBMAP_BITS_WORD - 1)
				 - bcm_count_leading_zeros(bitmap); /* use asm clz */

			mwbmap_p->ifree--; /* decrement system wide free count */
			MWBMAP_ASSERT(mwbmap_p->ifree >= 0);

			MWBMAP_DBG((
			    "Lvl2: bitix<%02u> wordix<%02u>: %08x ^ %08x = %08x ifree %d",
			    bitix, wordix, *bitmap_p, bitmap, (*bitmap_p) ^ bitmap,
			    mwbmap_p->ifree));

			*bitmap_p ^= bitmap; /* mark as allocated = 1b0 */

			return bitix;
		}
	}

	ASSERT(mwbmap_p->ifree == 0);

	return BCM_MWBMAP_INVALID_IDX;
}

/* Force an index at a specified position to be in use */
void
bcm_mwbmap_force(struct bcm_mwbmap * mwbmap_hdl, uint32 bitix)
{
	bcm_mwbmap_t * mwbmap_p;
	uint32 count, wordix, bitmap, *bitmap_p;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	ASSERT(bitix < mwbmap_p->total);

	/* Start with second hierarchy */
	wordix   = BCM_MWBMAP_DIVOP(bitix);
	bitmap   = (uint32)(1U << BCM_MWBMAP_MODOP(bitix));
	bitmap_p = &mwbmap_p->id_bitmap[wordix];

	ASSERT((*bitmap_p & bitmap) == bitmap);

	mwbmap_p->ifree--; /* update free count */
	ASSERT(mwbmap_p->ifree >= 0);

	MWBMAP_DBG(("Lvl2: bitix<%u> wordix<%u>: %08x ^ %08x = %08x ifree %d",
	            bitix, wordix, *bitmap_p, bitmap, (*bitmap_p) ^ bitmap,
	            mwbmap_p->ifree));

	*bitmap_p ^= bitmap; /* mark as in use */

	/* Update first hierarchy */
	bitix    = wordix;

	wordix   = BCM_MWBMAP_DIVOP(bitix);
	bitmap_p = &mwbmap_p->wd_bitmap[wordix];

#if defined(BCM_MWBMAP_USE_CNTSETBITS)
	count = bcm_cntsetbits(mwbmap_p->id_bitmap[bitix]);
#else  /* ! BCM_MWBMAP_USE_CNTSETBITS */
	mwbmap_p->wd_count[bitix]--;
	count = mwbmap_p->wd_count[bitix];
	MWBMAP_ASSERT(count == bcm_cntsetbits(mwbmap_p->id_bitmap[bitix]));
#endif /* ! BCM_MWBMAP_USE_CNTSETBITS */
	MWBMAP_ASSERT(count >= 0);

	bitmap   = (count == 0) << BCM_MWBMAP_MODOP(bitix);

	MWBMAP_DBG(("Lvl1: bitix<%02lu> wordix<%02u>: %08x ^ %08x = %08x wfree %d",
	            BCM_MWBMAP_MODOP(bitix), wordix, *bitmap_p, bitmap,
	            (*bitmap_p) ^ bitmap, count));

	*bitmap_p ^= bitmap; /* mark as in use */

	return;
}

/* Free a previously allocated index back into the multiword bitmap allocator */
void
BCMPOSTTRAPFASTPATH(bcm_mwbmap_free)(struct bcm_mwbmap * mwbmap_hdl, uint32 bitix)
{
	bcm_mwbmap_t * mwbmap_p;
	uint32 wordix, bitmap, *bitmap_p;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	ASSERT_FP(bitix < mwbmap_p->total);

	/* Start with second level hierarchy */
	wordix   = BCM_MWBMAP_DIVOP(bitix);
	bitmap   = (1U << BCM_MWBMAP_MODOP(bitix));
	bitmap_p = &mwbmap_p->id_bitmap[wordix];

	ASSERT_FP((*bitmap_p & bitmap) == 0U);	/* ASSERT not a double free */

	mwbmap_p->ifree++; /* update free count */
	ASSERT_FP(mwbmap_p->ifree <= mwbmap_p->total);

	MWBMAP_DBG(("Lvl2: bitix<%02u> wordix<%02u>: %08x | %08x = %08x ifree %d",
	            bitix, wordix, *bitmap_p, bitmap, (*bitmap_p) | bitmap,
	            mwbmap_p->ifree));

	*bitmap_p |= bitmap; /* mark as available */

	/* Now update first level hierarchy */

	bitix    = wordix;

	wordix   = BCM_MWBMAP_DIVOP(bitix); /* first level's word index */
	bitmap   = (1U << BCM_MWBMAP_MODOP(bitix));
	bitmap_p = &mwbmap_p->wd_bitmap[wordix];

#if !defined(BCM_MWBMAP_USE_CNTSETBITS)
	mwbmap_p->wd_count[bitix]++;
#endif

#if defined(BCM_MWBMAP_DEBUG)
	{
		uint32 count;
#if defined(BCM_MWBMAP_USE_CNTSETBITS)
		count = bcm_cntsetbits(mwbmap_p->id_bitmap[bitix]);
#else  /*  ! BCM_MWBMAP_USE_CNTSETBITS */
		count = mwbmap_p->wd_count[bitix];
		MWBMAP_ASSERT(count == bcm_cntsetbits(mwbmap_p->id_bitmap[bitix]));
#endif /*  ! BCM_MWBMAP_USE_CNTSETBITS */

		MWBMAP_ASSERT(count <= BCM_MWBMAP_BITS_WORD);

		MWBMAP_DBG(("Lvl1: bitix<%02u> wordix<%02u>: %08x | %08x = %08x wfree %d",
		            bitix, wordix, *bitmap_p, bitmap, (*bitmap_p) | bitmap, count));
	}
#endif /* BCM_MWBMAP_DEBUG */

	*bitmap_p |= bitmap;

	return;
}

/* Fetch the toal number of free indices in the multiword bitmap allocator */
uint32
bcm_mwbmap_free_cnt(struct bcm_mwbmap * mwbmap_hdl)
{
	bcm_mwbmap_t * mwbmap_p;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	ASSERT(mwbmap_p->ifree >= 0);

	return mwbmap_p->ifree;
}

/* Determine whether an index is inuse or free */
bool
bcm_mwbmap_isfree(struct bcm_mwbmap * mwbmap_hdl, uint32 bitix)
{
	bcm_mwbmap_t * mwbmap_p;
	uint32 wordix, bitmap;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	ASSERT(bitix < mwbmap_p->total);

	wordix   = BCM_MWBMAP_DIVOP(bitix);
	bitmap   = (1U << BCM_MWBMAP_MODOP(bitix));

	return ((mwbmap_p->id_bitmap[wordix] & bitmap) != 0U);
}

/* Debug dump a multiword bitmap allocator */
void
bcm_mwbmap_show(struct bcm_mwbmap * mwbmap_hdl)
{
	uint32 ix, count;
	bcm_mwbmap_t * mwbmap_p;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	printf("mwbmap_p %p wmaps %u imaps %u ifree %d total %u\n",
		OSL_OBFUSCATE_BUF((void *)mwbmap_p),
	       mwbmap_p->wmaps, mwbmap_p->imaps, mwbmap_p->ifree, mwbmap_p->total);
	for (ix = 0U; ix < mwbmap_p->wmaps; ix++) {
		printf("\tWDMAP:%2u. 0x%08x\t", ix, mwbmap_p->wd_bitmap[ix]);
		bcm_bitprint32(mwbmap_p->wd_bitmap[ix]);
		printf("\n");
	}
	for (ix = 0U; ix < mwbmap_p->imaps; ix++) {
#if defined(BCM_MWBMAP_USE_CNTSETBITS)
		count = bcm_cntsetbits(mwbmap_p->id_bitmap[ix]);
#else  /* ! BCM_MWBMAP_USE_CNTSETBITS */
		count = mwbmap_p->wd_count[ix];
		MWBMAP_ASSERT(count == bcm_cntsetbits(mwbmap_p->id_bitmap[ix]));
#endif /* ! BCM_MWBMAP_USE_CNTSETBITS */
		printf("\tIDMAP:%2u. 0x%08x %02u\t", ix, mwbmap_p->id_bitmap[ix], count);
		bcm_bitprint32(mwbmap_p->id_bitmap[ix]);
		printf("\n");
	}

	return;
}

/* Audit a hierarchical multiword bitmap */
void
bcm_mwbmap_audit(struct bcm_mwbmap * mwbmap_hdl)
{
	bcm_mwbmap_t * mwbmap_p;
	uint32 count, free_cnt = 0U, wordix, idmap_ix, bitix, *bitmap_p;

	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	for (wordix = 0U; wordix < mwbmap_p->wmaps; ++wordix) {

		bitmap_p = &mwbmap_p->wd_bitmap[wordix];

		for (bitix = 0U; bitix < BCM_MWBMAP_BITS_WORD; bitix++) {
			if ((*bitmap_p) & (1 << bitix)) {
				idmap_ix = BCM_MWBMAP_MULOP(wordix) + bitix;
#if defined(BCM_MWBMAP_USE_CNTSETBITS)
				count = bcm_cntsetbits(mwbmap_p->id_bitmap[idmap_ix]);
#else  /* ! BCM_MWBMAP_USE_CNTSETBITS */
				count = mwbmap_p->wd_count[idmap_ix];
				ASSERT(count == bcm_cntsetbits(mwbmap_p->id_bitmap[idmap_ix]));
#endif /* ! BCM_MWBMAP_USE_CNTSETBITS */
				ASSERT(count != 0U);
				free_cnt += count;
			}
		}
	}

	ASSERT((int)free_cnt == mwbmap_p->ifree);
}
/* END : Multiword bitmap based 64bit to Unique 32bit Id allocator. */

/* Simple 16bit Id allocator using a stack implementation. */
typedef struct id16_map {
	uint32  failures;  /* count of failures */
	void    *dbg;      /* debug placeholder */
	uint16  total;     /* total number of ids managed by allocator */
	uint16  start;     /* start value of 16bit ids to be managed */
	int     stack_idx; /* index into stack of available ids */
	uint16  stack[0];  /* stack of 16 bit ids */
} id16_map_t;

#define ID16_MAP_SZ(items)      (sizeof(id16_map_t) + \
				     (sizeof(uint16) * (items)))

#if defined(BCM_DBG)

/* Uncomment BCM_DBG_ID16 to debug double free */
/* #define BCM_DBG_ID16 */

typedef struct id16_map_dbg {
	uint16  total;
	bool    avail[0];
} id16_map_dbg_t;
#define ID16_MAP_DBG_SZ(items)  (sizeof(id16_map_dbg_t) + \
				     (sizeof(bool) * (items)))
#define ID16_MAP_MSG(x)         print x
#else
#define ID16_MAP_MSG(x)
#endif /* BCM_DBG */

void * /* Construct an id16 allocator: [start_val16 .. start_val16+total_ids) */
id16_map_init(osl_t *osh, uint16 total_ids, uint16 start_val16)
{
	uint16 idx, val16;
	id16_map_t * id16_map;

	ASSERT(total_ids > 0);

	/* A start_val16 of ID16_UNDEFINED, allows the caller to fill the id16 map
	 * with random values.
	 */
	ASSERT((start_val16 == ID16_UNDEFINED) ||
	       (start_val16 + total_ids) < ID16_INVALID);

	id16_map = (id16_map_t *) MALLOC(osh, ID16_MAP_SZ(total_ids));
	if (id16_map == NULL) {
		return NULL;
	}

	id16_map->total = total_ids;
	id16_map->start = start_val16;
	id16_map->failures = 0;
	id16_map->dbg = NULL;

	/*
	 * Populate stack with 16bit id values, commencing with start_val16.
	 * if start_val16 is ID16_UNDEFINED, then do not populate the id16 map.
	 */
	id16_map->stack_idx = -1;

	if (id16_map->start != ID16_UNDEFINED) {
		val16 = start_val16;

		for (idx = 0; idx < total_ids; idx++, val16++) {
			id16_map->stack_idx = idx;
			id16_map->stack[id16_map->stack_idx] = val16;
		}
	}

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	if (id16_map->start != ID16_UNDEFINED) {
		id16_map->dbg = MALLOC(osh, ID16_MAP_DBG_SZ(total_ids));

		if (id16_map->dbg) {
			id16_map_dbg_t *id16_map_dbg = (id16_map_dbg_t *)id16_map->dbg;

			id16_map_dbg->total = total_ids;
			for (idx = 0; idx < total_ids; idx++) {
				id16_map_dbg->avail[idx] = TRUE;
			}
		}
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */

	return (void *)id16_map;
}

void * /* Destruct an id16 allocator instance */
id16_map_fini(osl_t *osh, void * id16_map_hndl)
{
	uint16 total_ids;
	id16_map_t * id16_map;

	if (id16_map_hndl == NULL)
		return NULL;

	id16_map = (id16_map_t *)id16_map_hndl;

	total_ids = id16_map->total;
	ASSERT(total_ids > 0);

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	if (id16_map->dbg) {
		MFREE(osh, id16_map->dbg, ID16_MAP_DBG_SZ(total_ids));
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */

	id16_map->total = 0;
	MFREE(osh, id16_map, ID16_MAP_SZ(total_ids));

	return NULL;
}

void
id16_map_clear(void * id16_map_hndl, uint16 total_ids, uint16 start_val16)
{
	uint16 idx, val16;
	id16_map_t * id16_map;

	ASSERT(total_ids > 0);
	/* A start_val16 of ID16_UNDEFINED, allows the caller to fill the id16 map
	 * with random values.
	 */
	ASSERT((start_val16 == ID16_UNDEFINED) ||
	       (start_val16 + total_ids) < ID16_INVALID);

	id16_map = (id16_map_t *)id16_map_hndl;
	if (id16_map == NULL) {
		return;
	}

	id16_map->total = total_ids;
	id16_map->start = start_val16;
	id16_map->failures = 0;

	/* Populate stack with 16bit id values, commencing with start_val16 */
	id16_map->stack_idx = -1;

	if (id16_map->start != ID16_UNDEFINED) {
		val16 = start_val16;

		for (idx = 0; idx < total_ids; idx++, val16++) {
			id16_map->stack_idx = idx;
			id16_map->stack[id16_map->stack_idx] = val16;
		}
	}

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	if (id16_map->start != ID16_UNDEFINED) {
		if (id16_map->dbg) {
			id16_map_dbg_t *id16_map_dbg = (id16_map_dbg_t *)id16_map->dbg;

			id16_map_dbg->total = total_ids;
			for (idx = 0; idx < total_ids; idx++) {
				id16_map_dbg->avail[idx] = TRUE;
			}
		}
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */
}

uint16 /* Allocate a unique 16bit id */
BCMFASTPATH(id16_map_alloc)(void * id16_map_hndl)
{
	uint16 val16;
	id16_map_t * id16_map;

	ASSERT_FP(id16_map_hndl != NULL);

	id16_map = (id16_map_t *)id16_map_hndl;

	ASSERT_FP(id16_map->total > 0);

	if (id16_map->stack_idx < 0) {
		id16_map->failures++;
		return ID16_INVALID;
	}

	val16 = id16_map->stack[id16_map->stack_idx];
	id16_map->stack_idx--;

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	ASSERT_FP((id16_map->start == ID16_UNDEFINED) ||
	       (val16 < (id16_map->start + id16_map->total)));

	if (id16_map->dbg) { /* Validate val16 */
		id16_map_dbg_t *id16_map_dbg = (id16_map_dbg_t *)id16_map->dbg;

		ASSERT_FP(id16_map_dbg->avail[val16 - id16_map->start] == TRUE);
		id16_map_dbg->avail[val16 - id16_map->start] = FALSE;
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */

	return val16;
}

void /* Free a 16bit id value into the id16 allocator */
BCMFASTPATH(id16_map_free)(void * id16_map_hndl, uint16 val16)
{
	id16_map_t * id16_map;

	ASSERT_FP(id16_map_hndl != NULL);

	id16_map = (id16_map_t *)id16_map_hndl;

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	ASSERT_FP((id16_map->start == ID16_UNDEFINED) ||
	       (val16 < (id16_map->start + id16_map->total)));

	if (id16_map->dbg) { /* Validate val16 */
		id16_map_dbg_t *id16_map_dbg = (id16_map_dbg_t *)id16_map->dbg;

		ASSERT_FP(id16_map_dbg->avail[val16 - id16_map->start] == FALSE);
		id16_map_dbg->avail[val16 - id16_map->start] = TRUE;
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */

	id16_map->stack_idx++;
	id16_map->stack[id16_map->stack_idx] = val16;
}

uint32 /* Returns number of failures to allocate an unique id16 */
id16_map_failures(void * id16_map_hndl)
{
	ASSERT(id16_map_hndl != NULL);
	return ((id16_map_t *)id16_map_hndl)->failures;
}

bool
id16_map_audit(void * id16_map_hndl)
{
	int idx;
	int insane = 0;
	id16_map_t * id16_map;

	ASSERT(id16_map_hndl != NULL);

	id16_map = (id16_map_t *)id16_map_hndl;

	ASSERT(id16_map->stack_idx >= -1);
	ASSERT(id16_map->stack_idx < (int)id16_map->total);

	if (id16_map->start == ID16_UNDEFINED)
		goto done;

	for (idx = 0; idx <= id16_map->stack_idx; idx++) {
		ASSERT(id16_map->stack[idx] >= id16_map->start);
		ASSERT(id16_map->stack[idx] < (id16_map->start + id16_map->total));

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
		if (id16_map->dbg) {
			uint16 val16 = id16_map->stack[idx];
			if (((id16_map_dbg_t *)(id16_map->dbg))->avail[val16] != TRUE) {
				insane |= 1;
				ID16_MAP_MSG(("id16_map<%p>: stack_idx %u invalid val16 %u\n",
				              OSL_OBFUSATE_BUF(id16_map_hndl), idx, val16));
			}
		}
#endif /* BCM_DBG && BCM_DBG_ID16 */
	}

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	if (id16_map->dbg) {
		uint16 avail = 0; /* Audit available ids counts */
		for (idx = 0; idx < id16_map_dbg->total; idx++) {
			if (((id16_map_dbg_t *)(id16_map->dbg))->avail[idx16] == TRUE)
				avail++;
		}
		if (avail && (avail != (id16_map->stack_idx + 1))) {
			insane |= 1;
			ID16_MAP_MSG(("id16_map<%p>: avail %u stack_idx %u\n",
			              OSL_OBFUSCATE_BUF(id16_map_hndl),
			              avail, id16_map->stack_idx));
		}
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */

done:
	/* invoke any other system audits */
	return (!!insane);
}
/* END: Simple id16 allocator */

void
BCMATTACHFN(dll_pool_detach)(void * osh, dll_pool_t * pool, uint16 elems_max, uint16 elem_size)
{
	uint32 mem_size;
	mem_size = sizeof(dll_pool_t) + (elems_max * elem_size);
	if (pool)
		MFREE(osh, pool, mem_size);
}
dll_pool_t *
BCMATTACHFN(dll_pool_init)(void * osh, uint16 elems_max, uint16 elem_size)
{
	uint32 mem_size, i;
	dll_pool_t * dll_pool_p;
	dll_t * elem_p;

	ASSERT(elem_size > sizeof(dll_t));

	mem_size = sizeof(dll_pool_t) + (elems_max * elem_size);

	if ((dll_pool_p = (dll_pool_t *)MALLOCZ(osh, mem_size)) == NULL) {
		ASSERT(0);
		return dll_pool_p;
	}

	dll_init(&dll_pool_p->free_list);
	dll_pool_p->elems_max = elems_max;
	dll_pool_p->elem_size = elem_size;

	elem_p = dll_pool_p->elements;
	for (i = 0; i < elems_max; i++) {
		dll_append(&dll_pool_p->free_list, elem_p);
		elem_p = (dll_t *)((uintptr)elem_p + elem_size);
	}

	dll_pool_p->free_count = elems_max;

	return dll_pool_p;
}

void *
dll_pool_alloc(dll_pool_t * dll_pool_p)
{
	dll_t * elem_p;

	if (dll_pool_p->free_count == 0) {
		ASSERT(dll_empty(&dll_pool_p->free_list));
		return NULL;
	}

	elem_p = dll_head_p(&dll_pool_p->free_list);
	dll_delete(elem_p);
	dll_pool_p->free_count -= 1;

	return (void *)elem_p;
}

void
BCMPOSTTRAPFN(dll_pool_free)(dll_pool_t * dll_pool_p, void * elem_p)
{
	dll_t * node_p = (dll_t *)elem_p;
	dll_prepend(&dll_pool_p->free_list, node_p);
	dll_pool_p->free_count += 1;
}

void
dll_pool_free_tail(dll_pool_t * dll_pool_p, void * elem_p)
{
	dll_t * node_p = (dll_t *)elem_p;
	dll_append(&dll_pool_p->free_list, node_p);
	dll_pool_p->free_count += 1;
}

#ifdef BCMDBG
void
dll_pool_dump(dll_pool_t * dll_pool_p, dll_elem_dump elem_dump)
{
	dll_t * elem_p;
	dll_t * next_p;
	printf("dll_pool<%p> free_count<%u> elems_max<%u> elem_size<%u>\n",
		OSL_OBFUSCATE_BUF(dll_pool_p), dll_pool_p->free_count,
		dll_pool_p->elems_max, dll_pool_p->elem_size);

	for (elem_p = dll_head_p(&dll_pool_p->free_list);
		 !dll_end(&dll_pool_p->free_list, elem_p); elem_p = next_p) {

		next_p = dll_next_p(elem_p);
		printf("\telem<%p>\n", OSL_OBFUSCATE_BUF(elem_p));
		if (elem_dump != NULL)
			elem_dump((void *)elem_p);
	}
}
#endif /* BCMDBG */

#endif /* BCMDRIVER */

#if defined(BCMDRIVER) || defined(WL_UNITTEST)

/* triggers bcm_bprintf to print to kernel log */
bool bcm_bprintf_bypass = FALSE;

/* Initialization of bcmstrbuf structure */
void
BCMPOSTTRAPFN(bcm_binit)(struct bcmstrbuf *b, char *buf, uint size)
{
	b->origsize = b->size = size;
	b->origbuf = b->buf = buf;
	if (size > 0) {
		buf[0] = '\0';
	}
}

/* Buffer sprintf wrapper to guard against buffer overflow */
int
BCMPOSTTRAPFN(bcm_bprintf)(struct bcmstrbuf *b, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);

	r = vsnprintf(b->buf, b->size, fmt, ap);
	if (bcm_bprintf_bypass == TRUE) {
		printf("%s", b->buf);
		goto exit;
	}

	/* Non Ansi C99 compliant returns -1,
	 * Ansi compliant return r >= b->size,
	 * bcmstdlib returns 0, handle all
	 */
	/* r == 0 is also the case when strlen(fmt) is zero.
	 * typically the case when "" is passed as argument.
	 */
	if ((r == -1) || (r >= (int)b->size)) {
		b->size = 0;
	} else {
		b->size -= r;
		b->buf += r;
	}

exit:
	va_end(ap);

	return r;
}

void
bcm_bprhex(struct bcmstrbuf *b, const char *msg, bool newline, const uint8 *buf, uint len)
{
	uint i;

	if (msg != NULL && msg[0] != '\0')
		bcm_bprintf(b, "%s", msg);
	for (i = 0u; i < len; i ++)
		bcm_bprintf(b, "%02X", buf[i]);
	if (newline)
		bcm_bprintf(b, "\n");
}

void
bcm_inc_bytes(uchar *num, int num_bytes, uint8 amount)
{
	int i;

	for (i = 0; i < num_bytes; i++) {
		num[i] += amount;
		if (num[i] >= amount)
			break;
		amount = 1;
	}
}

int
bcm_cmp_bytes(const uchar *arg1, const uchar *arg2, uint8 nbytes)
{
	int i;

	for (i = nbytes - 1; i >= 0; i--) {
		if (arg1[i] != arg2[i])
			return (arg1[i] - arg2[i]);
	}
	return 0;
}

void
bcm_print_bytes(const char *name, const uchar *data, uint len)
{
	uint i;
	int per_line = 0;

	printf("%s: %d \n", name ? name : "", len);
	for (i = 0u; i < len; i++) {
		printf("%02x ", *data++);
		per_line++;
		if (per_line == 16) {
			per_line = 0;
			printf("\n");
		}
	}
	printf("\n");
}

/* Search for an IE having a specific tag and an OUI type from a buffer.
 * tlvs: buffer to search for IE
 * tlvs_len: buffer length
 * tag: IE tag
 * oui: Specific OUI to match
 * oui_len: length of the OUI
 * type: OUI type
 * Return the matched IE, else return null.
*/
bcm_tlv_t *
bcm_find_ie(const uint8* tlvs, uint tlvs_len, uint8 tag, uint8 oui_len,
	const char *oui, uint8 type)
{
	const bcm_tlv_t *ie;

	COV_TAINTED_DATA_SINK(tlvs_len);
	COV_NEG_SINK(tlvs_len);

	/* Walk through the IEs looking for an OUI match */
	while ((ie = bcm_parse_tlvs_advance(&tlvs, &tlvs_len, tag,
		BCM_TLV_ADVANCE_TO))) {
		if ((ie->len > oui_len) &&
		    !bcmp(ie->data, oui, oui_len) &&
			ie->data[oui_len] == type) {

			COV_TAINTED_DATA_ARG(ie);

			GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
			return (bcm_tlv_t *)(ie);		/* a match */
			GCC_DIAGNOSTIC_POP();
		}
		/* Point to the next IE */
		bcm_tlv_buffer_advance_past(ie, &tlvs, &tlvs_len);
	}

	return NULL;
}

/* Look for vendor-specific IE with specified OUI and optional type */
bcm_tlv_t *
bcm_find_vendor_ie(const  void *tlvs, uint tlvs_len, const char *voui, uint8 *type, uint type_len)
{
	const  bcm_tlv_t *ie;
	uint8 ie_len;

	COV_TAINTED_DATA_SINK(tlvs_len);
	COV_NEG_SINK(tlvs_len);

	ie = (const  bcm_tlv_t*)tlvs;

	/* make sure we are looking at a valid IE */
	if (ie == NULL || !bcm_valid_tlv(ie, tlvs_len)) {
		return NULL;
	}

	/* Walk through the IEs looking for an OUI match */
	do {
		ie_len = ie->len;
		if ((ie->id == DOT11_MNG_VS_ID) &&
		    (ie_len >= (DOT11_OUI_LEN + type_len)) &&
		    !bcmp(ie->data, voui, DOT11_OUI_LEN))
		{
			/* compare optional type */
			if (type_len == 0 ||
			    !bcmp(&ie->data[DOT11_OUI_LEN], type, type_len)) {

				COV_TAINTED_DATA_ARG(ie);

				GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
				return (bcm_tlv_t *)(ie);		/* a match */
				GCC_DIAGNOSTIC_POP();
			}
		}
	} while ((ie = bcm_next_tlv(ie, &tlvs_len)) != NULL);

	return NULL;
}

#if defined(WLTINYDUMP) || defined(BCMDBG) || defined(WLMSG_INFORM) || \
	defined(WLMSG_ASSOC) || defined(WLMSG_PRPKT) || defined(WLMSG_WSEC)
#define SSID_FMT_BUF_LEN	((4 * DOT11_MAX_SSID_LEN) + 1)

int
bcm_format_ssid(char* buf, const uchar ssid[], uint ssid_len)
{
	uint i, c;
	char *p = buf;
	char *endp = buf + SSID_FMT_BUF_LEN;

	if (ssid_len > DOT11_MAX_SSID_LEN) ssid_len = DOT11_MAX_SSID_LEN;

	for (i = 0; i < ssid_len; i++) {
		c = (uint)ssid[i];
		if (c == '\\') {
			*p++ = '\\';
			*p++ = '\\';
		} else if (bcm_isprint((uchar)c)) {
			*p++ = (char)c;
		} else {
			p += snprintf(p, (endp - p), "\\x%02X", c);
		}
	}
	*p = '\0';
	ASSERT(p < endp);

	return (int)(p - buf);
}
#endif /* WLTINYDUMP || BCMDBG || WLMSG_INFORM || WLMSG_ASSOC || WLMSG_PRPKT */

#endif /* BCMDRIVER || WL_UNITTEST */

/* Masking few bytes of MAC address per customer in all prints/eventlogs. */
int
BCMRAMFN(bcm_addrmask_set)(int enable)
{
#ifdef PRIVACY_MASK
	struct ether_addr *privacy = privacy_addrmask_get();
	if (enable) {
		/* apply mask as (For SS)
		 * orig		: 12:34:56:78:90:ab
		 * masked	: 12:xx:xx:xx:x0:ab
		 */
		privacy->octet[1] = privacy->octet[2] =
			privacy->octet[3] = 0;
		privacy->octet[0] = privacy->octet[5] = 0xff;
		privacy->octet[4] = 0x0f;
	} else
	{
		/* No masking. All are 0xff. */
		memcpy(privacy, &ether_bcast, sizeof(struct ether_addr));
	}

	return BCME_OK;
#else
	BCM_REFERENCE(enable);
	return BCME_UNSUPPORTED;
#endif /* PRIVACY_MASK */

}

int
bcm_addrmask_get(int *val)
{
#ifdef PRIVACY_MASK
	struct ether_addr *privacy = privacy_addrmask_get();
	if (!eacmp(&ether_bcast, privacy)) {
		*val = FALSE;
	} else {
		*val = TRUE;
	}

	return BCME_OK;
#else
	BCM_REFERENCE(val);
	return BCME_UNSUPPORTED;
#endif
}

uint64
BCMRAMFN(bcm_ether_ntou64)(const struct ether_addr *ea)
{
	uint64 mac;
	struct ether_addr addr;

	memcpy(&addr, ea, sizeof(struct ether_addr));

#ifdef PRIVACY_MASK
	struct ether_addr *privacy = privacy_addrmask_get();
	if (!ETHER_ISMULTI(ea)) {
		*(uint32*)(&addr.octet[0]) &= *((uint32*)&privacy->octet[0]);
		*(uint16*)(&addr.octet[4]) &= *((uint16*)&privacy->octet[4]);
	}
#endif /*  PRIVACY_MASK */

	mac = ((uint64)HTON16(*((const uint16*)&addr.octet[4]))) << 32 |
		HTON32(*((const uint32*)&addr.octet[0]));
	return (mac);
}

char *
bcm_ether_ntoa(const struct ether_addr *ea, char *buf)
{
	static const char hex[] =
	  {
		  '0', '1', '2', '3', '4', '5', '6', '7',
		  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	  };
	const uint8 *octet = ea->octet;
	char *p = buf;
	int i;

	for (i = 0; i < 6; i++, octet++) {
		*p++ = hex[(*octet >> 4) & 0xf];
		*p++ = hex[*octet & 0xf];
		*p++ = ':';
	}

	*(p-1) = '\0';

	return (buf);
}

/* Find the position of first bit set
 * in the given number.
 */
int
bcm_find_fsb(uint32 num)
{
	uint8 pos = 0;
	if (!num)
		return pos;
	while (!(num & 1)) {
		num >>= 1;
		pos++;
	}
	return (pos+1);
}

/* TODO: need to pass in the buffer length for validation check */
char *
bcm_ip_ntoa(struct ipv4_addr *ia, char *buf)
{
	snprintf(buf, 16, "%d.%d.%d.%d",
	         ia->addr[0], ia->addr[1], ia->addr[2], ia->addr[3]);
	return (buf);
}

/* TODO: need to pass in the buffer length for validation check */
char *
bcm_ipv6_ntoa(void *ipv6, char *buf)
{
	/* Implementing RFC 5952 Sections 4 + 5 */
	/* Not thoroughly tested */
	uint16 tmp[8];
	uint16 *a = &tmp[0];
	char *p = buf;
	int i, i_max = -1, cnt = 0, cnt_max = 1;
	uint8 *a4 = NULL;
	memcpy((uint8 *)&tmp[0], (uint8 *)ipv6, IPV6_ADDR_LEN);

	for (i = 0; i < IPV6_ADDR_LEN/2; i++) {
		if (a[i]) {
			if (cnt > cnt_max) {
				cnt_max = cnt;
				i_max = i - cnt;
			}
			cnt = 0;
		} else
			cnt++;
	}
	if (cnt > cnt_max) {
		cnt_max = cnt;
		i_max = i - cnt;
	}
	if (i_max == 0 &&
		/* IPv4-translated: ::ffff:0:a.b.c.d */
		((cnt_max == 4 && a[4] == 0xffff && a[5] == 0) ||
		/* IPv4-mapped: ::ffff:a.b.c.d */
		(cnt_max == 5 && a[5] == 0xffff)))
		a4 = (uint8*) (a + 6);

	for (i = 0; i < IPV6_ADDR_LEN/2; i++) {
		if ((uint8*) (a + i) == a4) {
			snprintf(p, 17, ":%u.%u.%u.%u", a4[0], a4[1], a4[2], a4[3]);
			break;
		} else if (i == i_max) {
			*p++ = ':';
			i += cnt_max - 1;
			p[0] = ':';
			p[1] = '\0';
		} else {
			if (i)
				*p++ = ':';
			p += snprintf(p, 8, "%x", ntoh16(a[i]));
		}
	}

	return buf;
}

#if !defined(BCMROMOFFLOAD_EXCLUDE_BCMUTILS_FUNCS)
const unsigned char bcm_ctype[256] = {

	_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,			/* 0-7 */
	_BCM_C, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C|_BCM_S, _BCM_C,
	_BCM_C,	/* 8-15 */
	_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,			/* 16-23 */
	_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,_BCM_C,			/* 24-31 */
	_BCM_S|_BCM_SP,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,		/* 32-39 */
	_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,			/* 40-47 */
	_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,_BCM_D,			/* 48-55 */
	_BCM_D,_BCM_D,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,			/* 56-63 */
	_BCM_P, _BCM_U|_BCM_X, _BCM_U|_BCM_X, _BCM_U|_BCM_X, _BCM_U|_BCM_X, _BCM_U|_BCM_X,
	_BCM_U|_BCM_X, _BCM_U, /* 64-71 */
	_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,			/* 72-79 */
	_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,_BCM_U,			/* 80-87 */
	_BCM_U,_BCM_U,_BCM_U,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_P,			/* 88-95 */
	_BCM_P, _BCM_L|_BCM_X, _BCM_L|_BCM_X, _BCM_L|_BCM_X, _BCM_L|_BCM_X, _BCM_L|_BCM_X,
	_BCM_L|_BCM_X, _BCM_L, /* 96-103 */
	_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L, /* 104-111 */
	_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L,_BCM_L, /* 112-119 */
	_BCM_L,_BCM_L,_BCM_L,_BCM_P,_BCM_P,_BCM_P,_BCM_P,_BCM_C, /* 120-127 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 128-143 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 144-159 */
	_BCM_S|_BCM_SP, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,
	_BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,	/* 160-175 */
	_BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,
	_BCM_P, _BCM_P, _BCM_P, _BCM_P, _BCM_P,	/* 176-191 */
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U,
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U,	/* 192-207 */
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_P, _BCM_U, _BCM_U, _BCM_U,
	_BCM_U, _BCM_U, _BCM_U, _BCM_U, _BCM_L,	/* 208-223 */
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L,
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L,	/* 224-239 */
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_P, _BCM_L, _BCM_L, _BCM_L,
	_BCM_L, _BCM_L, _BCM_L, _BCM_L, _BCM_L /* 240-255 */
};

uint64
bcm_strtoull(const char *cp, char **endp, uint base)
{
	uint64 result, last_result = 0, value;
	bool minus;

	minus = FALSE;

	while (bcm_isspace(*cp))
		cp++;

	if (cp[0] == '+')
		cp++;
	else if (cp[0] == '-') {
		minus = TRUE;
		cp++;
	}

	if (base == 0) {
		if (cp[0] == '0') {
			if ((cp[1] == 'x') || (cp[1] == 'X')) {
				base = 16;
				cp = &cp[2];
			} else {
				base = 8;
				cp = &cp[1];
			}
		} else
			base = 10;
	} else if (base == 16 && (cp[0] == '0') && ((cp[1] == 'x') || (cp[1] == 'X'))) {
		cp = &cp[2];
	}

	result = 0;

	while (bcm_isxdigit(*cp) &&
	       (value = bcm_isdigit(*cp) ? *cp-'0' : bcm_toupper(*cp)-'A'+10) < base) {
		result = result*base + value;
		/* Detected overflow */
		if (result < last_result && !minus) {
			if (endp) {
				/* Go to the end of current number */
				while (bcm_isxdigit(*cp)) {
					cp++;
				}
				*endp = DISCARD_QUAL(cp, char);
			}
			return (ulong)-1;
		}
		last_result = result;
		cp++;
	}

	if (minus)
		result = (ulong)(-(long)result);

	if (endp)
		*endp = DISCARD_QUAL(cp, char);

	return (result);
}

ulong
bcm_strtoul(const char *cp, char **endp, uint base)
{
	return (ulong) bcm_strtoull(cp, endp, base);
}

int
bcm_atoi(const char *s)
{
	return (int)bcm_strtoul(s, NULL, 10);
}

/* return pointer to location of substring 'needle' in 'haystack' */
char *
bcmstrstr(const char *haystack, const char *needle)
{
	uint len, nlen;
	uint i;

	if ((haystack == NULL) || (needle == NULL))
		return DISCARD_QUAL(haystack, char);

	nlen = (uint)strlen(needle);
	if (strlen(haystack) < nlen) {
		return NULL;
	}
	len = (uint)strlen(haystack) - nlen + 1u;

	for (i = 0u; i < len; i++)
		if (memcmp(needle, &haystack[i], nlen) == 0)
			return DISCARD_QUAL(&haystack[i], char);
	return (NULL);
}

char *
bcmstrnstr(const char *s, uint s_len, const char *substr, uint substr_len)
{
	for (; s_len >= substr_len; s++, s_len--)
		if (strncmp(s, substr, substr_len) == 0)
			return DISCARD_QUAL(s, char);

	return NULL;
}

char *
bcmstrcat(char *dest, const char *src)
{
	char *p;

	p = dest + strlen(dest);

	while ((*p++ = *src++) != '\0')
		;

	return (dest);
}

char *
bcmstrncat(char *dest, const char *src, uint size)
{
	char *endp;
	char *p;

	p = dest + strlen(dest);
	endp = p + size;

	while (p != endp && (*p++ = *src++) != '\0')
		;

	return (dest);
}

/****************************************************************************
* Function:   bcmstrtok
*
* Purpose:
*  Tokenizes a string. This function is conceptually similiar to ANSI C strtok(),
*  but allows strToken() to be used by different strings or callers at the same
*  time. Each call modifies '*string' by substituting a NULL character for the
*  first delimiter that is encountered, and updates 'string' to point to the char
*  after the delimiter. Leading delimiters are skipped.
*
* Parameters:
*  string      (mod) Ptr to string ptr, updated by token.
*  delimiters  (in)  Set of delimiter characters.
*  tokdelim    (out) Character that delimits the returned token. (May
*                    be set to NULL if token delimiter is not required).
*
* Returns:  Pointer to the next token found. NULL when no more tokens are found.
*****************************************************************************
*/
char *
bcmstrtok(char **string, const char *delimiters, char *tokdelim)
{
	unsigned char *str;
	unsigned long map[8];
	int count;
	char *nextoken;

	if (tokdelim != NULL) {
		/* Prime the token delimiter */
		*tokdelim = '\0';
	}

	/* Clear control map */
	for (count = 0; count < 8; count++) {
		map[count] = 0;
	}

	/* Set bits in delimiter table */
	do {
		map[*delimiters >> 5] |= (1 << (*delimiters & 31));
	}
	while (*delimiters++);

	str = (unsigned char*)*string;

	/* Find beginning of token (skip over leading delimiters). Note that
	 * there is no token iff this loop sets str to point to the terminal
	 * null (*str == '\0')
	 */
	while (((map[*str >> 5] & (1 << (*str & 31))) && *str) || (*str == ' ')) {
		str++;
	}

	nextoken = (char*)str;

	/* Find the end of the token. If it is not the end of the string,
	 * put a null there.
	 */
	for (; *str; str++) {
		if (map[*str >> 5] & (1 << (*str & 31))) {
			if (tokdelim != NULL) {
				*tokdelim = *str;
			}

			*str++ = '\0';
			break;
		}
	}

	*string = (char*)str;

	/* Determine if a token has been found. */
	if (nextoken == (char *) str) {
		return NULL;
	}
	else {
		return nextoken;
	}
}

#define xToLower(C) \
	((C >= 'A' && C <= 'Z') ? (char)((int)C - (int)'A' + (int)'a') : C)

/****************************************************************************
* Function:   bcmstricmp
*
* Purpose:    Compare to strings case insensitively.
*
* Parameters: s1 (in) First string to compare.
*             s2 (in) Second string to compare.
*
* Returns:    Return 0 if the two strings are equal, -1 if t1 < t2 and 1 if
*             t1 > t2, when ignoring case sensitivity.
*****************************************************************************
*/
int
bcmstricmp(const char *s1, const char *s2)
{
	char dc, sc;

	while (*s2 && *s1) {
		dc = xToLower(*s1);
		sc = xToLower(*s2);
		if (dc < sc) return -1;
		if (dc > sc) return 1;
		s1++;
		s2++;
	}

	if (*s1 && !*s2) return 1;
	if (!*s1 && *s2) return -1;
	return 0;
}

/****************************************************************************
* Function:   bcmstrnicmp
*
* Purpose:    Compare to strings case insensitively, upto a max of 'cnt'
*             characters.
*
* Parameters: s1  (in) First string to compare.
*             s2  (in) Second string to compare.
*             cnt (in) Max characters to compare.
*
* Returns:    Return 0 if the two strings are equal, -1 if t1 < t2 and 1 if
*             t1 > t2, when ignoring case sensitivity.
*****************************************************************************
*/
int
bcmstrnicmp(const char* s1, const char* s2, int cnt)
{
	char dc, sc;

	while (*s2 && *s1 && cnt) {
		dc = xToLower(*s1);
		sc = xToLower(*s2);
		if (dc < sc) return -1;
		if (dc > sc) return 1;
		s1++;
		s2++;
		cnt--;
	}

	if (!cnt) return 0;
	if (*s1 && !*s2) return 1;
	if (!*s1 && *s2) return -1;
	return 0;
}

/* parse a xx:xx:xx:xx:xx:xx format ethernet address */
int
bcm_ether_atoe(const char *p, struct ether_addr *ea)
{
	int i = 0;
	char *ep;

	for (;;) {
		ea->octet[i++] = (char) bcm_strtoul(p, &ep, 16);
		p = ep;
		if (!*p++ || i == 6)
			break;
	}

	return (i == 6);
}

/* parse a nnn.nnn.nnn.nnn format IPV4 address */
int
bcm_atoipv4(const char *p, struct ipv4_addr *ip)
{

	int i = 0;
	char *c;
	for (;;) {
		ip->addr[i++] = (uint8)bcm_strtoul(p, &c, 0);
		if (*c++ != '.' || i == IPV4_ADDR_LEN)
			break;
		p = c;
	}
	return (i == IPV4_ADDR_LEN);
}
#endif	/* !BCMROMOFFLOAD_EXCLUDE_BCMUTILS_FUNCS */

const struct ether_addr ether_bcast = {{255, 255, 255, 255, 255, 255}};
const struct ether_addr ether_null = {{0, 0, 0, 0, 0, 0}};
const struct ether_addr ether_ipv6_mcast = {{0x33, 0x33, 0x00, 0x00, 0x00, 0x01}};

int
ether_isbcast(const void *ea)
{
	return (memcmp(ea, &ether_bcast, sizeof(struct ether_addr)) == 0);
}

int
BCMPOSTTRAPFN(ether_isnulladdr)(const void *ea)
{
	const uint8 *ea8 = (const uint8 *)ea;
	return !(ea8[5] || ea8[4] || ea8[3] || ea8[2] || ea8[1] || ea8[0]);
}

#if defined(CONFIG_USBRNDIS_RETAIL) || defined(NDIS_MINIPORT_DRIVER)
/* registry routine buffer preparation utility functions:
 * parameter order is like strlcpy, but returns count
 * of bytes copied. Minimum bytes copied is null char(1)/wchar(2)
 */
ulong
wchar2ascii(char *abuf, ushort *wbuf, ushort wbuflen, ulong abuflen)
{
	ulong copyct = 1;
	ushort i;

	if (abuflen == 0)
		return 0;

	/* wbuflen is in bytes */
	wbuflen /= sizeof(ushort);

	for (i = 0; i < wbuflen; ++i) {
		if (--abuflen == 0)
			break;
		*abuf++ = (char) *wbuf++;
		++copyct;
	}
	*abuf = '\0';

	return copyct;
}
#endif /* CONFIG_USBRNDIS_RETAIL || NDIS_MINIPORT_DRIVER */

#ifdef BCM_OBJECT_TRACE

#define BCM_OBJECT_MERGE_SAME_OBJ	0

/* some place may add / remove the object to trace list for Linux: */
/* add:    osl_alloc_skb dev_alloc_skb skb_realloc_headroom dhd_start_xmit */
/* remove: osl_pktfree dev_kfree_skb netif_rx */

#if defined(__linux__)
#define BCM_OBJDBG_COUNT          (1024 * 100)
static spinlock_t dbgobj_lock;
#define	BCM_OBJDBG_LOCK_INIT()    spin_lock_init(&dbgobj_lock)
#define	BCM_OBJDBG_LOCK_DESTROY()
#define	BCM_OBJDBG_LOCK           spin_lock_irqsave
#define	BCM_OBJDBG_UNLOCK         spin_unlock_irqrestore
#else
#define BCM_OBJDBG_COUNT          (256)
#define BCM_OBJDBG_LOCK_INIT()
#define	BCM_OBJDBG_LOCK_DESTROY()
#define BCM_OBJDBG_LOCK(x, y)
#define BCM_OBJDBG_UNLOCK(x, y)
#endif /* else OS */

#define BCM_OBJDBG_ADDTOHEAD      0
#define BCM_OBJDBG_ADDTOTAIL      1

#define BCM_OBJDBG_CALLER_LEN     32
struct bcm_dbgobj {
	struct bcm_dbgobj *prior;
	struct bcm_dbgobj *next;
	uint32 flag;
	void   *obj;
	uint32 obj_sn;
	uint32 obj_state;
	uint32 line;
	char   caller[BCM_OBJDBG_CALLER_LEN];
};

static struct bcm_dbgobj *dbgobj_freehead = NULL;
static struct bcm_dbgobj *dbgobj_freetail = NULL;
static struct bcm_dbgobj *dbgobj_objhead = NULL;
static struct bcm_dbgobj *dbgobj_objtail = NULL;

static uint32 dbgobj_sn = 0;
static int dbgobj_count = 0;
static struct bcm_dbgobj bcm_dbg_objs[BCM_OBJDBG_COUNT];

void
bcm_object_trace_init(void)
{
	int i = 0;
	BCM_OBJDBG_LOCK_INIT();
	memset(&bcm_dbg_objs, 0x00, sizeof(struct bcm_dbgobj) * BCM_OBJDBG_COUNT);
	dbgobj_freehead = &bcm_dbg_objs[0];
	dbgobj_freetail = &bcm_dbg_objs[BCM_OBJDBG_COUNT - 1];

	for (i = 0; i < BCM_OBJDBG_COUNT; ++i) {
		bcm_dbg_objs[i].next = (i == (BCM_OBJDBG_COUNT - 1)) ?
			dbgobj_freehead : &bcm_dbg_objs[i + 1];
		bcm_dbg_objs[i].prior = (i == 0) ?
			dbgobj_freetail : &bcm_dbg_objs[i - 1];
	}
}

void
bcm_object_trace_deinit(void)
{
	if (dbgobj_objhead || dbgobj_objtail) {
		printf("bcm_object_trace_deinit: not all objects are released\n");
		ASSERT(0);
	}
	BCM_OBJDBG_LOCK_DESTROY();
}

static void
bcm_object_rm_list(struct bcm_dbgobj **head, struct bcm_dbgobj **tail,
	struct bcm_dbgobj *dbgobj)
{
	if ((dbgobj == *head) && (dbgobj == *tail)) {
		*head = NULL;
		*tail = NULL;
	} else if (dbgobj == *head) {
		*head = (*head)->next;
	} else if (dbgobj == *tail) {
		*tail = (*tail)->prior;
	}
	dbgobj->next->prior = dbgobj->prior;
	dbgobj->prior->next = dbgobj->next;
}

static void
bcm_object_add_list(struct bcm_dbgobj **head, struct bcm_dbgobj **tail,
	struct bcm_dbgobj *dbgobj, int addtotail)
{
	if (!(*head) && !(*tail)) {
		*head = dbgobj;
		*tail = dbgobj;
		dbgobj->next = dbgobj;
		dbgobj->prior = dbgobj;
	} else if ((*head) && (*tail)) {
		(*tail)->next = dbgobj;
		(*head)->prior = dbgobj;
		dbgobj->next = *head;
		dbgobj->prior = *tail;
		if (addtotail == BCM_OBJDBG_ADDTOTAIL)
			*tail = dbgobj;
		else
			*head = dbgobj;
	} else {
		ASSERT(0); /* can't be this case */
	}
}

static INLINE void
bcm_object_movetoend(struct bcm_dbgobj **head, struct bcm_dbgobj **tail,
	struct bcm_dbgobj *dbgobj, int movetotail)
{
	if ((*head) && (*tail)) {
		if (movetotail == BCM_OBJDBG_ADDTOTAIL) {
			if (dbgobj != (*tail)) {
				bcm_object_rm_list(head, tail, dbgobj);
				bcm_object_add_list(head, tail, dbgobj, movetotail);
			}
		} else {
			if (dbgobj != (*head)) {
				bcm_object_rm_list(head, tail, dbgobj);
				bcm_object_add_list(head, tail, dbgobj, movetotail);
			}
		}
	} else {
		ASSERT(0); /* can't be this case */
	}
}

void
bcm_object_trace_opr(void *obj, uint32 opt, const char *caller, int line)
{
	struct bcm_dbgobj *dbgobj;
	unsigned long flags;

	BCM_REFERENCE(flags);
	BCM_OBJDBG_LOCK(&dbgobj_lock, flags);

	if (opt == BCM_OBJDBG_ADD_PKT ||
		opt == BCM_OBJDBG_ADD) {
		dbgobj = dbgobj_objtail;
		while (dbgobj) {
			if (dbgobj->obj == obj) {
				printf("bcm_object_trace_opr: obj %p allocated from %s(%d),"
					" allocate again from %s(%d)\n",
					dbgobj->obj,
					dbgobj->caller, dbgobj->line,
					caller, line);
				ASSERT(0);
				goto EXIT;
			}
			dbgobj = dbgobj->prior;
			if (dbgobj == dbgobj_objtail)
				break;
		}

#if BCM_OBJECT_MERGE_SAME_OBJ
		dbgobj = dbgobj_freetail;
		while (dbgobj) {
			if (dbgobj->obj == obj) {
				goto FREED_ENTRY_FOUND;
			}
			dbgobj = dbgobj->prior;
			if (dbgobj == dbgobj_freetail)
				break;
		}
#endif /* BCM_OBJECT_MERGE_SAME_OBJ */

		dbgobj = dbgobj_freehead;
#if BCM_OBJECT_MERGE_SAME_OBJ
FREED_ENTRY_FOUND:
#endif /* BCM_OBJECT_MERGE_SAME_OBJ */
		if (!dbgobj) {
			printf("bcm_object_trace_opr: already got %d objects ?????????????????\n",
				BCM_OBJDBG_COUNT);
			ASSERT(0);
			goto EXIT;
		}

		bcm_object_rm_list(&dbgobj_freehead, &dbgobj_freetail, dbgobj);
		dbgobj->obj = obj;
		strlcpy(dbgobj->caller, caller, sizeof(dbgobj->caller));
		dbgobj->line = line;
		dbgobj->flag = 0;
		if (opt == BCM_OBJDBG_ADD_PKT) {
			dbgobj->obj_sn = dbgobj_sn++;
			dbgobj->obj_state = 0;
			/* first 4 bytes is pkt sn */
			if (((unsigned long)PKTTAG(obj)) & 0x3)
				printf("pkt tag address not aligned by 4: %p\n", PKTTAG(obj));
			*(uint32*)PKTTAG(obj) = dbgobj->obj_sn;
		}
		bcm_object_add_list(&dbgobj_objhead, &dbgobj_objtail, dbgobj,
			BCM_OBJDBG_ADDTOTAIL);

		dbgobj_count++;

	} else if (opt == BCM_OBJDBG_REMOVE) {
		dbgobj = dbgobj_objtail;
		while (dbgobj) {
			if (dbgobj->obj == obj) {
				if (dbgobj->flag) {
					printf("bcm_object_trace_opr: rm flagged obj %p"
						" flag 0x%08x from %s(%d)\n",
						obj, dbgobj->flag, caller, line);
				}
				bcm_object_rm_list(&dbgobj_objhead, &dbgobj_objtail, dbgobj);
				bzero(dbgobj->caller, sizeof(dbgobj->caller));
				strlcpy(dbgobj->caller, caller, sizeof(dbgobj->caller));
				dbgobj->line = line;
				bcm_object_add_list(&dbgobj_freehead, &dbgobj_freetail, dbgobj,
					BCM_OBJDBG_ADDTOTAIL);
				dbgobj_count--;
				goto EXIT;
			}
			dbgobj = dbgobj->prior;
			if (dbgobj == dbgobj_objtail)
				break;
		}

		dbgobj = dbgobj_freetail;
		while (dbgobj && dbgobj->obj) {
			if (dbgobj->obj == obj) {
				printf("bcm_object_trace_opr: obj %p already freed"
					" from from %s(%d),"
					" try free again from %s(%d)\n",
					obj,
					dbgobj->caller, dbgobj->line,
					caller, line);
				//ASSERT(0); /* release same obj more than one time? */
				goto EXIT;
			}
			dbgobj = dbgobj->prior;
			if (dbgobj == dbgobj_freetail)
				break;
		}

		printf("bcm_object_trace_opr: ################### release none-existing"
			" obj %p from %s(%d)\n",
			obj, caller, line);
		//ASSERT(0); /* release same obj more than one time? */

	}

EXIT:
	BCM_OBJDBG_UNLOCK(&dbgobj_lock, flags);
	return;
}

void
bcm_object_trace_upd(void *obj, void *obj_new)
{
	struct bcm_dbgobj *dbgobj;
	unsigned long flags;

	BCM_REFERENCE(flags);
	BCM_OBJDBG_LOCK(&dbgobj_lock, flags);

	dbgobj = dbgobj_objtail;
	while (dbgobj) {
		if (dbgobj->obj == obj) {
			dbgobj->obj = obj_new;
			if (dbgobj != dbgobj_objtail) {
				bcm_object_movetoend(&dbgobj_objhead, &dbgobj_objtail,
					dbgobj, BCM_OBJDBG_ADDTOTAIL);
			}
			goto EXIT;
		}
		dbgobj = dbgobj->prior;
		if (dbgobj == dbgobj_objtail)
			break;
	}

EXIT:
	BCM_OBJDBG_UNLOCK(&dbgobj_lock, flags);
	return;
}

void
bcm_object_trace_chk(void *obj, uint32 chksn, uint32 sn,
	const char *caller, int line)
{
	struct bcm_dbgobj *dbgobj;
	unsigned long flags;

	BCM_REFERENCE(flags);
	BCM_OBJDBG_LOCK(&dbgobj_lock, flags);

	dbgobj = dbgobj_objtail;
	while (dbgobj) {
		if ((dbgobj->obj == obj) &&
			((!chksn) || (dbgobj->obj_sn == sn))) {
#if 0
			printf("bcm_object_trace_chk: (%s:%d) obj %p was allocated from %s(%d)\n",
				caller, line,
				dbgobj->obj, dbgobj->caller, dbgobj->line);
#endif /* #if 0 */
			if (dbgobj != dbgobj_objtail) {
				bcm_object_movetoend(&dbgobj_objhead, &dbgobj_objtail,
					dbgobj, BCM_OBJDBG_ADDTOTAIL);
			}
			goto EXIT;
		}
		dbgobj = dbgobj->prior;
		if (dbgobj == dbgobj_objtail)
			break;
	}

	dbgobj = dbgobj_freetail;
	while (dbgobj) {
		if ((dbgobj->obj == obj) &&
			((!chksn) || (dbgobj->obj_sn == sn))) {
			printf("bcm_object_trace_chk: (%s:%d) obj %p (sn %d state %d)"
				" was freed from %s(%d)\n",
				caller, line,
				dbgobj->obj, dbgobj->obj_sn, dbgobj->obj_state,
				dbgobj->caller, dbgobj->line);
			goto EXIT;
		}
		else if (dbgobj->obj == NULL) {
			break;
		}
		dbgobj = dbgobj->prior;
		if (dbgobj == dbgobj_freetail)
			break;
	}

	printf("bcm_object_trace_chk: obj %p not found, check from %s(%d), chksn %s, sn %d\n",
		obj, caller, line, chksn ? "yes" : "no", sn);
	dbgobj = dbgobj_objtail;
	while (dbgobj) {
		printf("bcm_object_trace_chk: (%s:%d) obj %p sn %d was allocated from %s(%d)\n",
				caller, line,
				dbgobj->obj, dbgobj->obj_sn, dbgobj->caller, dbgobj->line);
		dbgobj = dbgobj->prior;
		if (dbgobj == dbgobj_objtail)
			break;
	}

EXIT:
	BCM_OBJDBG_UNLOCK(&dbgobj_lock, flags);
	return;
}

void
bcm_object_feature_set(void *obj, uint32 type, uint32 value)
{
	struct bcm_dbgobj *dbgobj;
	unsigned long flags;

	BCM_REFERENCE(flags);
	BCM_OBJDBG_LOCK(&dbgobj_lock, flags);

	dbgobj = dbgobj_objtail;
	while (dbgobj) {
		if (dbgobj->obj == obj) {
			if (type == BCM_OBJECT_FEATURE_FLAG) {
				if (value & BCM_OBJECT_FEATURE_CLEAR)
					dbgobj->flag &= ~(value);
				else
					dbgobj->flag |= (value);
			} else if (type == BCM_OBJECT_FEATURE_PKT_STATE) {
				dbgobj->obj_state = value;
			}
			if (dbgobj != dbgobj_objtail) {
				bcm_object_movetoend(&dbgobj_objhead, &dbgobj_objtail,
					dbgobj, BCM_OBJDBG_ADDTOTAIL);
			}
			goto EXIT;
		}
		dbgobj = dbgobj->prior;
		if (dbgobj == dbgobj_objtail)
			break;
	}

	printf("bcm_object_feature_set: obj %p not found in active list\n", obj);
	ASSERT(0);

EXIT:
	BCM_OBJDBG_UNLOCK(&dbgobj_lock, flags);
	return;
}

int
bcm_object_feature_get(void *obj, uint32 type, uint32 value)
{
	int rtn = 0;
	struct bcm_dbgobj *dbgobj;
	unsigned long flags;

	BCM_REFERENCE(flags);
	BCM_OBJDBG_LOCK(&dbgobj_lock, flags);

	dbgobj = dbgobj_objtail;
	while (dbgobj) {
		if (dbgobj->obj == obj) {
			if (type == BCM_OBJECT_FEATURE_FLAG) {
				rtn = (dbgobj->flag & value) & (~BCM_OBJECT_FEATURE_CLEAR);
			}
			if (dbgobj != dbgobj_objtail) {
				bcm_object_movetoend(&dbgobj_objhead, &dbgobj_objtail,
					dbgobj, BCM_OBJDBG_ADDTOTAIL);
			}
			goto EXIT;
		}
		dbgobj = dbgobj->prior;
		if (dbgobj == dbgobj_objtail)
			break;
	}

	printf("bcm_object_feature_get: obj %p not found in active list\n", obj);
	ASSERT(0);

EXIT:
	BCM_OBJDBG_UNLOCK(&dbgobj_lock, flags);
	return rtn;
}

#endif /* BCM_OBJECT_TRACE */

uint8 *
BCMPOSTTRAPFN(bcm_write_tlv)(int type, const void *data, uint datalen, uint8 *dst)
{
	uint8 *new_dst = dst;
	bcm_tlv_t *dst_tlv = (bcm_tlv_t *)dst;

	/* dst buffer should always be valid */
	ASSERT(dst);

	/* data len must be within valid range */
	ASSERT((datalen <= BCM_TLV_MAX_DATA_SIZE));

	/* source data buffer pointer should be valid, unless datalen is 0
	 * meaning no data with this TLV
	 */
	ASSERT((data != NULL) || (datalen == 0));

	/* only do work if the inputs are valid
	 * - must have a dst to write to AND
	 * - datalen must be within range AND
	 * - the source data pointer must be non-NULL if datalen is non-zero
	 * (this last condition detects datalen > 0 with a NULL data pointer)
	 */
	if ((dst != NULL) &&
	    ((datalen <= BCM_TLV_MAX_DATA_SIZE)) &&
	    ((data != NULL) || (datalen == 0u))) {

		/* write type, len fields */
		dst_tlv->id = (uint8)type;
		dst_tlv->len = (uint8)datalen;

		/* if data is present, copy to the output buffer and update
		 * pointer to output buffer
		 */
		if (datalen > 0u) {

			memcpy(dst_tlv->data, data, datalen);
		}

		/* update the output destination poitner to point past
		 * the TLV written
		 */
		new_dst = dst + BCM_TLV_HDR_SIZE + datalen;
	}

	return (new_dst);
}

uint8 *
bcm_write_tlv_ext(uint8 type, uint8 ext, const void *data, uint8 datalen, uint8 *dst)
{
	uint8 *new_dst = dst;
	bcm_tlv_ext_t *dst_tlv = (bcm_tlv_ext_t *)dst;

	/* dst buffer should always be valid */
	ASSERT(dst);

	/* data len must be within valid range */
	ASSERT(datalen <= BCM_TLV_EXT_MAX_DATA_SIZE);

	/* source data buffer pointer should be valid, unless datalen is 0
	 * meaning no data with this TLV
	 */
	ASSERT((data != NULL) || (datalen == 0));

	/* only do work if the inputs are valid
	 * - must have a dst to write to AND
	 * - datalen must be within range AND
	 * - the source data pointer must be non-NULL if datalen is non-zero
	 * (this last condition detects datalen > 0 with a NULL data pointer)
	 */
	if ((dst != NULL) &&
	    (datalen <= BCM_TLV_EXT_MAX_DATA_SIZE) &&
	    ((data != NULL) || (datalen == 0))) {

		/* write type, len fields */
		dst_tlv->id = (uint8)type;
		dst_tlv->ext = ext;
		dst_tlv->len = 1 + (uint8)datalen;

		/* if data is present, copy to the output buffer and update
		 * pointer to output buffer
		 */
		if (datalen > 0) {
			memcpy(dst_tlv->data, data, datalen);
		}

		/* update the output destination poitner to point past
		 * the TLV written
		 */
		new_dst = dst + BCM_TLV_EXT_HDR_SIZE + datalen;
	}

	return (new_dst);
}

uint8 *
BCMPOSTTRAPFN(bcm_write_tlv_safe)(int type, const void *data, uint datalen, uint8 *dst,
	uint dst_maxlen)
{
	uint8 *new_dst = dst;

	if ((datalen <= BCM_TLV_MAX_DATA_SIZE)) {

		/* if len + tlv hdr len is more than destlen, don't do anything
		 * just return the buffer untouched
		 */
		if ((datalen + BCM_TLV_HDR_SIZE) <= dst_maxlen) {

			new_dst = bcm_write_tlv(type, data, datalen, dst);
		}
	}

	return (new_dst);
}

uint8 *
bcm_copy_tlv(const void *src, uint8 *dst)
{
	uint8 *new_dst = dst;
	const bcm_tlv_t *src_tlv = (const bcm_tlv_t *)src;
	uint totlen;

	ASSERT(dst && src);
	if (dst && src) {

		totlen = BCM_TLV_HDR_SIZE + src_tlv->len;
		memcpy(dst, src_tlv, totlen);
		new_dst = dst + totlen;
	}

	return (new_dst);
}

uint8 *
bcm_copy_tlv_safe(const void *src, uint8 *dst, uint dst_maxlen)
{
	uint8 *new_dst = dst;
	const bcm_tlv_t *src_tlv = (const bcm_tlv_t *)src;

	ASSERT(src);
	if (bcm_valid_tlv(src_tlv, dst_maxlen)) {
		new_dst = bcm_copy_tlv(src, dst);
	}

	return (new_dst);
}

#if !defined(BCMROMOFFLOAD_EXCLUDE_BCMUTILS_FUNCS)
/*******************************************************************************
 * crc8
 *
 * Computes a crc8 over the input data using the polynomial:
 *
 *       x^8 + x^7 +x^6 + x^4 + x^2 + 1
 *
 * The caller provides the initial value (either CRC8_INIT_VALUE
 * or the previous returned value) to allow for processing of
 * discontiguous blocks of data.  When generating the CRC the
 * caller is responsible for complementing the final return value
 * and inserting it into the byte stream.  When checking, a final
 * return value of CRC8_GOOD_VALUE indicates a valid CRC.
 *
 * Reference: Dallas Semiconductor Application Note 27
 *   Williams, Ross N., "A Painless Guide to CRC Error Detection Algorithms",
 *     ver 3, Aug 1993, ross@guest.adelaide.edu.au, Rocksoft Pty Ltd.,
 *     ftp://ftp.rocksoft.com/clients/rocksoft/papers/crc_v3.txt
 *
 * ****************************************************************************
 */

static const uint8 crc8_table[256] = {
    0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
    0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
    0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
    0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
    0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
    0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
    0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
    0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
    0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
    0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
    0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
    0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
    0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
    0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
    0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
    0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
    0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
    0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
    0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
    0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
    0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
    0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
    0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
    0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
    0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
    0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
    0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
    0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
    0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
    0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
    0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
    0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F
};

#define CRC_INNER_LOOP(n, c, x) \
	(c) = ((c) >> 8) ^ crc##n##_table[((c) ^ (x)) & 0xff]

uint8
hndcrc8(
	const uint8 *pdata,	/* pointer to array of data to process */
	uint  nbytes,	/* number of input data bytes to process */
	uint8 crc	/* either CRC8_INIT_VALUE or previous return value */
)
{
	/* hard code the crc loop instead of using CRC_INNER_LOOP macro
	 * to avoid the undefined and unnecessary (uint8 >> 8) operation.
	 */
	while (nbytes-- > 0)
		crc = crc8_table[(crc ^ *pdata++) & 0xff];

	return crc;
}

/*******************************************************************************
 * crc16
 *
 * Computes a crc16 over the input data using the polynomial:
 *
 *       x^16 + x^12 +x^5 + 1
 *
 * The caller provides the initial value (either CRC16_INIT_VALUE
 * or the previous returned value) to allow for processing of
 * discontiguous blocks of data.  When generating the CRC the
 * caller is responsible for complementing the final return value
 * and inserting it into the byte stream.  When checking, a final
 * return value of CRC16_GOOD_VALUE indicates a valid CRC.
 *
 * Reference: Dallas Semiconductor Application Note 27
 *   Williams, Ross N., "A Painless Guide to CRC Error Detection Algorithms",
 *     ver 3, Aug 1993, ross@guest.adelaide.edu.au, Rocksoft Pty Ltd.,
 *     ftp://ftp.rocksoft.com/clients/rocksoft/papers/crc_v3.txt
 *
 * ****************************************************************************
 */

static const uint16 crc16_table[256] = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

uint16
hndcrc16(
    const uint8 *pdata,  /* pointer to array of data to process */
    uint nbytes, /* number of input data bytes to process */
    uint16 crc     /* either CRC16_INIT_VALUE or previous return value */
)
{
	while (nbytes-- > 0)
		CRC_INNER_LOOP(16, crc, *pdata++);
	return crc;
}

static const uint32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

/*
 * crc input is CRC32_INIT_VALUE for a fresh start, or previous return value if
 * accumulating over multiple pieces.
 */
uint32
hndcrc32(const uint8 *pdata, uint nbytes, uint32 crc)
{
	const uint8 *pend;
	pend = pdata + nbytes;
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);

	return crc;
}

#ifdef NOT_YET
#define CLEN	1499	/*  CRC Length */
#define CBUFSIZ		(CLEN+4)
#define CNBUFS		5 /* # of bufs */

void
testcrc32(void)
{
	uint j, k, l;
	uint8 *buf;
	uint len[CNBUFS];
	uint32 crcr;
	uint32 crc32tv[CNBUFS] =
		{0xd2cb1faa, 0xd385c8fa, 0xf5b4f3f3, 0x55789e20, 0x00343110};

	ASSERT((buf = MALLOC(CBUFSIZ*CNBUFS)) != NULL);

	/* step through all possible alignments */
	for (l = 0; l <= 4; l++) {
		for (j = 0; j < CNBUFS; j++) {
			len[j] = CLEN;
			for (k = 0; k < len[j]; k++)
				*(buf + j*CBUFSIZ + (k+l)) = (j+k) & 0xff;
		}

		for (j = 0; j < CNBUFS; j++) {
			crcr = crc32(buf + j*CBUFSIZ + l, len[j], CRC32_INIT_VALUE);
			ASSERT(crcr == crc32tv[j]);
		}
	}

	MFREE(buf, CBUFSIZ*CNBUFS);
	return;
}
#endif /* NOT_YET */

/*
 * Advance from the current 1-byte tag/1-byte length/variable-length value
 * triple, to the next, returning a pointer to the next.
 * If the current or next TLV is invalid (does not fit in given buffer length),
 * NULL is returned.
 * *buflen is not modified if the TLV elt parameter is invalid, or is decremented
 * by the TLV parameter's length if it is valid.
 */
bcm_tlv_t *
bcm_next_tlv(const bcm_tlv_t *elt, uint *buflen)
{
	uint len;

	COV_TAINTED_DATA_SINK(buflen);
	COV_NEG_SINK(buflen);

	/* validate current elt */
	if (!bcm_valid_tlv(elt, *buflen)) {
		return NULL;
	}

	/* advance to next elt */
	len = TLV_HDR_LEN + elt->len;
	elt = (const  bcm_tlv_t*)((const uint8 *)elt + len);

#if defined(__COVERITY__)
	/* The 'len' value is tainted in Coverity because it is read from the tainted data pointed
	 * to by 'elt'.  However, bcm_valid_tlv() verifies that the elt pointer is a valid element,
	 * so its length, len = (TLV_HDR_LEN + elt->len), is in the bounds of the buffer.
	 * Clearing the tainted attribute of 'len' for Coverity.
	 */
	__coverity_tainted_data_sanitize__(len);
	if (len > *buflen) {
		return NULL;
	}
#endif /* __COVERITY__ */

	*buflen -= len;

	/* validate next elt */
	if (!bcm_valid_tlv(elt, *buflen)) {
		return NULL;
	}

	COV_TAINTED_DATA_ARG(elt);

	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	return (bcm_tlv_t *)(elt);
	GCC_DIAGNOSTIC_POP();
}

/**
 * Advance a const tlv buffer pointer and length up to the given tlv element pointer
 * 'elt'.  The function checks that elt is a valid tlv; the elt pointer and data
 * are all in the range of the buffer/length.
 *
 * @param elt      pointer to a valid bcm_tlv_t in the buffer
 * @param buffer   pointer to a tlv buffer
 * @param buflen   length of the buffer in bytes
 *
 * On return, if elt is not a tlv in the buffer bounds, the *buffer parameter
 * will be set to NULL and *buflen parameter will be set to zero.  Otherwise,
 * *buffer will point to elt, and *buflen will have been adjusted by the the
 * difference between *buffer and elt.
 */
void
bcm_tlv_buffer_advance_to(const bcm_tlv_t *elt, const uint8 **buffer, uint *buflen)
{
	uint new_buflen;
	const uint8 *new_buffer;

	/* model the input length value as a tainted and negative sink so
	 * Coverity will complain about unvalidated or possibly length values
	 */
	COV_TAINTED_DATA_SINK(*buflen);
	COV_NEG_SINK(*buflen);

	new_buffer = (const uint8*)elt;

	/* make sure the input buffer pointer is non-null, that (buffer + buflen) does not wrap,
	 * and that the elt pointer is in the range of [buffer, buffer + buflen]
	 */
	if ((*buffer != NULL) &&
	    ((uintptr)*buffer < ((uintptr)*buffer + *buflen)) &&
	    (new_buffer >= *buffer) &&
	    (new_buffer < (*buffer + *buflen))) {
		/* delta between buffer and new_buffer is <= *buflen, so truncating cast to uint
		 * from ptrdiff is ok
		 */
		uint delta = (uint)(new_buffer - *buffer);

		/* New buffer length is old len minus the delta from the buffer start to elt.
		 * The check just above guarantees that the subtractions does not underflow.
		 */
		new_buflen = *buflen - delta;

		/* validate current elt */
		if (bcm_valid_tlv(elt, new_buflen)) {
			/* All good, so update the input/output parameters */
			*buffer = new_buffer;
			*buflen = new_buflen;
			return;
		}
	}

	/* something did not check out, clear out the buffer info */
	*buffer = NULL;
	*buflen = 0;

	return;
}

/**
 * Advance a const tlv buffer pointer and length past the given tlv element pointer
 * 'elt'.  The function checks that elt is a valid tlv; the elt pointer and data
 * are all in the range of the buffer/length.  The function also checks that the
 * remaining buffer starts with a valid tlv.
 *
 * @param elt      pointer to a valid bcm_tlv_t in the buffer
 * @param buffer   pointer to a tlv buffer
 * @param buflen   length of the buffer in bytes
 *
 * On return, if elt is not a tlv in the buffer bounds, or the remaining buffer
 * following the elt does not begin with a tlv in the buffer bounds, the *buffer
 * parameter will be set to NULL and *buflen parameter will be set to zero.
 * Otherwise, *buffer will point to the first byte past elt, and *buflen will
 * have the remaining buffer length.
 */
void
bcm_tlv_buffer_advance_past(const bcm_tlv_t *elt, const uint8 **buffer, uint *buflen)
{
	/* Start by advancing the buffer up to the given elt */
	bcm_tlv_buffer_advance_to(elt, buffer, buflen);

	/* if that did not work, bail out */
	if (*buflen == 0) {
		return;
	}

#if defined(__COVERITY__)
	/* The elt has been verified by bcm_tlv_buffer_advance_to() to be a valid element,
	 * so its elt->len is in the bounds of the buffer. The following check prevents
	 * Coverity from flagging the (elt->data + elt->len) statement below as using a
	 * tainted elt->len to index into array 'elt->data'.
	 */
	if (elt->len > *buflen) {
		return;
	}
#endif /* __COVERITY__ */

	/* We know we are advanced up to a good tlv.
	 * Now just advance to the following tlv.
	 */
	elt = (const bcm_tlv_t*)(elt->data + elt->len);

	bcm_tlv_buffer_advance_to(elt, buffer, buflen);

	return;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
bcm_tlv_t *
bcm_parse_tlvs(const void *buf, uint buflen, uint key)
{
	const bcm_tlv_t *elt;
	uint totlen;

	COV_TAINTED_DATA_SINK(buflen);
	COV_NEG_SINK(buflen);

	if ((elt = (const bcm_tlv_t*)buf) == NULL) {
		return NULL;
	}
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		uint len = elt->len;

		/* check if elt overruns buffer */
		if (totlen < (len + TLV_HDR_LEN)) {
			break;
		}
		/* did we find the ID? */
		if ((elt->id == key)) {
			COV_TAINTED_DATA_ARG(elt);

			GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
			return (bcm_tlv_t *)(elt);
			GCC_DIAGNOSTIC_POP();
		}
		elt = (const bcm_tlv_t*)((const uint8*)elt + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);
	}

	return NULL;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag.
 * The 'advance' parmeter specifies what to do to the parse buf/buflen values if a
 * matching tlv is found:
 *	BCM_TLV_ADVANCE_NONE - do nothing
 *	BCM_TLV_ADVANCE_TO - move the buf up to the discovered tlv, and adjust buflen.
 *	BCM_TLV_ADVANCE_PAST - move the buf past the discovered tlb, and adjust buflen.
 * If a tlv is not found, no changes are made to buf/buflen
 *
 */
const bcm_tlv_t *
bcm_parse_tlvs_advance(const uint8 **buf, uint *buflen, uint key, bcm_tlv_advance_mode_t advance)
{
	const bcm_tlv_t *elt;

	elt = bcm_parse_tlvs(*buf, *buflen, key);

	if (elt == NULL) {
		return elt;
	}

	if (advance == BCM_TLV_ADVANCE_TO) {
		bcm_tlv_buffer_advance_to(elt, buf, buflen);
	} else if (advance == BCM_TLV_ADVANCE_PAST) {
		bcm_tlv_buffer_advance_past(elt, buf, buflen);
	} else if (advance == BCM_TLV_ADVANCE_NONE) {
		/* nothing to do */
	} else {
		/* there are only 3 modes, but just in case, zero the parse buffer pointer and
		 * length to prevent infinite loops in callers that expect progress.
		 */
		ASSERT(0);
		*buf = NULL;
		*buflen = 0;
	}

	return elt;
}

bcm_tlv_t *
bcm_parse_tlvs_dot11(const void *buf, uint buflen, uint key, bool id_ext)
{
	bcm_tlv_t *elt;
	uint totlen;

	COV_TAINTED_DATA_SINK(buflen);
	COV_NEG_SINK(buflen);

	/*
	   ideally, we don't want to do that, but returning a const pointer
	   from these parse function spreads casting everywhere in the code
	*/
	GCC_DIAGNOSTIC_PUSH_SUPPRESS_CAST();
	elt = (bcm_tlv_t*)buf;
	GCC_DIAGNOSTIC_POP();

	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		uint len = elt->len;

		/* validate remaining totlen */
		if (totlen < (len + TLV_HDR_LEN)) {
			break;
		}

		do {
			if (id_ext) {
				if (!DOT11_MNG_IE_ID_EXT_MATCH(elt, key))
					break;
			} else if (elt->id != key) {
				break;
			}

			COV_TAINTED_DATA_ARG(elt);

			return (bcm_tlv_t *)(elt);		/* a match */
		} while (0);

		elt = (bcm_tlv_t*)((uint8*)elt + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);
	}

	return NULL;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 * return NULL if not found or length field < min_varlen
 */
bcm_tlv_t *
bcm_parse_tlvs_min_bodylen(const  void *buf, uint buflen, uint key, uint min_bodylen)
{
	bcm_tlv_t * ret;
	ret = bcm_parse_tlvs(buf, buflen, key);
	if (ret == NULL || ret->len < min_bodylen) {
		return NULL;
	}
	return ret;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 * return NULL if not found or tlv size > max_len or < min_len
 */
bcm_tlv_t *
bcm_parse_tlvs_minmax_len(const  void *buf, uint buflen, uint key,
	uint min_len, uint max_len)
{
	bcm_tlv_t * ret;
	ret = bcm_parse_tlvs(buf, buflen, key);
	if (ret == NULL ||
		(BCM_TLV_SIZE(ret) > max_len) ||
		(BCM_TLV_SIZE(ret) < min_len)) {
		return NULL;
	}
	return ret;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag.  Stop parsing when we see an element whose ID is greater
 * than the target key.
 */
const  bcm_tlv_t *
bcm_parse_ordered_tlvs(const  void *buf, uint buflen, uint key)
{
	const  bcm_tlv_t *elt;
	uint totlen;

	COV_TAINTED_DATA_SINK(buflen);
	COV_NEG_SINK(buflen);

	elt = (const  bcm_tlv_t*)buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		uint id = elt->id;
		uint len = elt->len;

		/* Punt if we start seeing IDs > than target key */
		if (id > key) {
			return (NULL);
		}

		/* validate remaining totlen */
		if (totlen < (len + TLV_HDR_LEN)) {
			break;
		}
		if (id == key) {
			COV_TAINTED_DATA_ARG(elt);
			return (elt);
		}

		elt = (const  bcm_tlv_t*)((const  uint8*)elt + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);
	}
	return NULL;
}
#endif	/* !BCMROMOFFLOAD_EXCLUDE_BCMUTILS_FUNCS */

uint
bcm_format_field(const bcm_bit_desc_ex_t *bd, uint32 flags, char* buf, uint len)
{
	uint i, slen = 0;
	uint32 bit, mask;
	const char *name;
	mask = bd->mask;
	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0;  (name = bd->bitfield[i].name) != NULL; i++) {
		bit = bd->bitfield[i].bit;
		if ((flags & mask) == bit) {
			slen = (int)strlen(name);
			if (memcpy_s(buf, len, name, slen + 1) != BCME_OK) {
				slen = 0;
			}
			break;
		}
	}
	return slen;
}

int
bcm_format_flags(const bcm_bit_desc_t *bd, uint32 flags, char* buf, uint len)
{
	uint i;
	char *p = buf;
	char *end =  (buf + len);
	char hexstr[16];
	uint32 bit;
	const char* name;
	bool err = FALSE;

	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0; flags != 0; i++) {
		bit = bd[i].bit;
		name = bd[i].name;
		if (bit == 0 && flags != 0) {
			/* print any unnamed bits */
			snprintf(hexstr, sizeof(hexstr), "0x%X", flags);
			name = hexstr;
			flags = 0;	/* exit loop */
		} else if ((flags & bit) == 0) {
			continue;
		}
		flags &= ~bit;

		/* Print named bit. */
		p += strlcpy(p, name, (end - p));
		if (p == end) {
			/* Truncation error. */
			err = TRUE;
			break;
		}

		/* Add space delimiter if there are more bits. */
		if (flags != 0) {
			p += strlcpy(p, " ", (end - p));
			if (p == end) {
				/* Truncation error. */
				err = TRUE;
				break;
			}
		}
	}

	/* indicate the str was too short */
	if (err) {
		ASSERT(len >= 2u);
		buf[len - 2u] = '>';
	}

	return (int)(p - buf);
}

/* print out whcih bits in octet array 'addr' are set. bcm_bit_desc_t:bit is a bit offset. */
int
bcm_format_octets(const bcm_bit_desc_t *bd, uint bdsz,
	const uint8 *addr, uint size, char *buf, uint len)
{
	uint i;
	char *p = buf;
	uint slen = 0, nlen = 0;
	uint32 bit;
	const char* name;
	bool more = FALSE;

	BCM_REFERENCE(size);

	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0; i < bdsz; i++) {
		bit = bd[i].bit;
		name = bd[i].name;
		if (isset(addr, bit)) {
			nlen = (int)strlen(name);
			slen += nlen;
			/* need SPACE - for simplicity */
			slen += 1;
			/* need NULL as well */
			if (len < slen + 1) {
				more = TRUE;
				break;
			}
			memcpy(p, name, nlen);
			p += nlen;
			p[0] = ' ';
			p += 1;
			p[0] = '\0';
		}
	}

	if (more) {
		p[0] = '>';
		p += 1;
		p[0] = '\0';
	}

	return (int)(p - buf);
}

/* Transform an hexadecimal string into binary.
 * Output is limited to 64K.
 * hex : string
 * hex_len : string length
 * buf : allocated output buffer
 * buf_len : allocated size
 * return : copied length, if successfull, 0 if error.
 */
uint16
bcmhex2bin(const uint8* hex, uint hex_len, uint8 *buf, uint buf_len)
{
		uint i = 0;
		uint16 out_len;
		char tmp[] = "XX";
		if (hex_len % 2) {
			/* hex_len not even */
			return 0;
		}
		/* check for hex radix */
		if ((hex[0] == '0') && ((hex[1] == 'x') || (hex[1] == 'X'))) {
			hex += 2;
			hex_len -= 2;
		}
		if (hex_len/2 > 0xFFFF) {
			/* exceed 64K buffer capacity */
			return 0;
		}
		if ((out_len = hex_len/2) > buf_len) {
			/* buf too short */
			return 0;
		}
		do {
			tmp[0] = *hex++;
			tmp[1] = *hex++;
			if (!bcm_isxdigit(tmp[0]) || !bcm_isxdigit(tmp[1])) {
				/* char is not a 256-bit hex number	*/
				return 0;
			}
			/* okay so far; make this piece a number */
			buf[i] = (uint8) bcm_strtoul(tmp, NULL, 16);
		} while (++i < out_len);
		return out_len;
}

/* print bytes formatted as hex to a string. return the resulting string length */
int
bcm_format_hex(char *str, const void *bytes, uint len)
{
	uint i;
	char *p = str;
	const uint8 *src = (const uint8*)bytes;

	for (i = 0; i < len; i++) {
		p += snprintf(p, 3, "%02X", *src);
		src++;
	}
	return (int)(p - str);
}

/* pretty hex print a contiguous buffer */
void
prhex(const char *msg, const uchar *buf, uint nbytes)
{
	char line[128], *p;
	uint len = sizeof(line);
	int nchar;
	uint i;

	if (msg && (msg[0] != '\0'))
		printf("%s:\n", msg);

	p = line;
	for (i = 0; i < nbytes; i++) {
		if (i % 16 == 0) {
			nchar = snprintf(p, len, "  %04x: ", i);	/* line prefix */
			p += nchar;
			len -= nchar;
		}
		if (len > 0) {
			nchar = snprintf(p, len, "%02x ", buf[i]);
			p += nchar;
			len -= nchar;
		}

		if (i % 16 == 15) {
			printf("%s\n", line);		/* flush line */
			p = line;
			len = sizeof(line);
		}
	}

	/* flush last partial line */
	if (p != line)
		printf("%s\n", line);
}

static const char *crypto_algo_names[] = {
	"NONE",
	"WEP1",
	"TKIP",
	"WEP128",
	"AES_CCM",
	"AES_OCB_MSDU",
	"AES_OCB_MPDU",
	"NALG",
	"UNDEF",
	"UNDEF",
	"UNDEF",

#ifdef BCMWAPI_WAI
	"WAPI",
#endif /* BCMWAPI_WAI */

#ifndef BCMWAPI_WAI
	"UNDEF",
#endif
	"PMK",
	"BIP",
	"AES_GCM",
	"AES_CCM256",
	"AES_GCM256",
	"BIP_CMAC256",
	"BIP_GMAC",
	"BIP_GMAC256",
	"UNDEF"
};

const char *
bcm_crypto_algo_name(uint algo)
{
	return (algo < ARRAYSIZE(crypto_algo_names)) ? crypto_algo_names[algo] : "ERR";
}

#ifdef BCMDBG
void
deadbeef(void *p, uint len)
{
	static uint8 meat[] = { 0xde, 0xad, 0xbe, 0xef };

	while (len-- > 0) {
		*(uint8*)p = meat[((uintptr)p) & 3];
		p = (uint8*)p + 1;
	}
}
#endif /* BCMDBG */

char *
bcm_chipname(uint chipid, char *buf, uint len)
{
	const char *fmt;

	fmt = ((chipid > 0xa000) || (chipid < 0x4000)) ? "%d" : "%x";
	snprintf(buf, len, fmt, chipid);
	return buf;
}

/* Produce a human-readable string for boardrev */
char *
bcm_brev_str(uint32 brev, char *buf)
{
	if (brev < 0x100)
		snprintf(buf, 8, "%d.%d", (brev & 0xf0) >> 4, brev & 0xf);
	else
		snprintf(buf, 8, "%c%03x", ((brev & 0xf000) == 0x1000) ? 'P' : 'A', brev & 0xfff);

	return (buf);
}

#define BUFSIZE_TODUMP_ATONCE 128 /* Buffer size */

/* dump large strings to console */
void
printbig(char *buf)
{
	uint len, max_len;
	char c;

	len = (uint)strlen(buf);

	max_len = BUFSIZE_TODUMP_ATONCE;

	while (len > max_len) {
		c = buf[max_len];
		buf[max_len] = '\0';
		printf("%s", buf);
		buf[max_len] = c;

		buf += max_len;
		len -= max_len;
	}
	/* print the remaining string */
	printf("%s\n", buf);
	return;
}

/* routine to dump fields in a fileddesc structure */
uint
bcmdumpfields(bcmutl_rdreg_rtn read_rtn, void *arg0, uint arg1, struct fielddesc *fielddesc_array,
	char *buf, uint32 bufsize)
{
	uint  filled_len;
	int len;
	struct fielddesc *cur_ptr;

	filled_len = 0;
	cur_ptr = fielddesc_array;

	while (bufsize > 1) {
		if (cur_ptr->nameandfmt == NULL)
			break;
		len = snprintf(buf, bufsize, cur_ptr->nameandfmt,
		               read_rtn(arg0, arg1, cur_ptr->offset));
		/* check for snprintf overflow or error */
		if (len < 0 || (uint32)len >= bufsize)
			len = bufsize - 1;
		buf += len;
		bufsize -= len;
		filled_len += len;
		cur_ptr++;
	}
	return filled_len;
}

uint
bcm_mkiovar(const char *name, const char *data, uint datalen, char *buf, uint buflen)
{
	uint len;

	len = (uint)strlen(name) + 1;

	if ((len + datalen) > buflen)
		return 0;

	strlcpy(buf, name, buflen);

	/* append data onto the end of the name string */
	if (data && datalen != 0) {
		memcpy(&buf[len], data, datalen);
		len += datalen;
	}

	return len;
}

/* Quarter dBm units to mW
 * Table starts at QDBM_OFFSET, so the first entry is mW for qdBm=153
 * Table is offset so the last entry is largest mW value that fits in
 * a uint16.
 */

#define QDBM_OFFSET 153		/* Offset for first entry */
#define QDBM_TABLE_LEN 40	/* Table size */

/* Smallest mW value that will round up to the first table entry, QDBM_OFFSET.
 * Value is ( mW(QDBM_OFFSET - 1) + mW(QDBM_OFFSET) ) / 2
 */
#define QDBM_TABLE_LOW_BOUND 6493 /* Low bound */

/* Largest mW value that will round down to the last table entry,
 * QDBM_OFFSET + QDBM_TABLE_LEN-1.
 * Value is ( mW(QDBM_OFFSET + QDBM_TABLE_LEN - 1) + mW(QDBM_OFFSET + QDBM_TABLE_LEN) ) / 2.
 */
#define QDBM_TABLE_HIGH_BOUND 64938 /* High bound */

static const uint16 nqdBm_to_mW_map[QDBM_TABLE_LEN] = {
/* qdBm:	+0	+1	+2	+3	+4	+5	+6	+7 */
/* 153: */      6683,	7079,	7499,	7943,	8414,	8913,	9441,	10000,
/* 161: */      10593,	11220,	11885,	12589,	13335,	14125,	14962,	15849,
/* 169: */      16788,	17783,	18836,	19953,	21135,	22387,	23714,	25119,
/* 177: */      26607,	28184,	29854,	31623,	33497,	35481,	37584,	39811,
/* 185: */      42170,	44668,	47315,	50119,	53088,	56234,	59566,	63096
};

uint16
bcm_qdbm_to_mw(uint8 qdbm)
{
	uint factor = 1;
	int idx = qdbm - QDBM_OFFSET;

	if (idx >= QDBM_TABLE_LEN) {
		/* clamp to max uint16 mW value */
		return 0xFFFF;
	}

	/* scale the qdBm index up to the range of the table 0-40
	 * where an offset of 40 qdBm equals a factor of 10 mW.
	 */
	while (idx < 0) {
		idx += 40;
		factor *= 10;
	}

	/* return the mW value scaled down to the correct factor of 10,
	 * adding in factor/2 to get proper rounding.
	 */
	return ((nqdBm_to_mW_map[idx] + factor/2) / factor);
}

uint8
bcm_mw_to_qdbm(uint16 mw)
{
	uint8 qdbm;
	int offset;
	uint mw_uint = mw;
	uint boundary;

	/* handle boundary case */
	if (mw_uint <= 1)
		return 0;

	offset = QDBM_OFFSET;

	/* move mw into the range of the table */
	while (mw_uint < QDBM_TABLE_LOW_BOUND) {
		mw_uint *= 10;
		offset -= 40;
	}

	for (qdbm = 0; qdbm < QDBM_TABLE_LEN-1; qdbm++) {
		boundary = nqdBm_to_mW_map[qdbm] + (nqdBm_to_mW_map[qdbm+1] -
		                                    nqdBm_to_mW_map[qdbm])/2;
		if (mw_uint < boundary) break;
	}

	qdbm += (uint8)offset;

	return (qdbm);
}

uint
BCMPOSTTRAPFN(bcm_bitcount)(const uint8 *bitmap, uint length)
{
	uint bitcount = 0, i;
	uint8 tmp;
	for (i = 0; i < length; i++) {
		tmp = bitmap[i];
		while (tmp) {
			bitcount++;
			tmp &= (tmp - 1);
		}
	}
	return bitcount;
}

static void
dump_nvram(char *varbuf, int column, unsigned int n, unsigned int len)
{
	unsigned int m;
	char vars[128];

	if (((n==0) && (varbuf[0]=='#')) ||
			((column==0) && (android_msg_level & ANDROID_INFO_LEVEL))) {
		memset(vars, 0x00, sizeof(vars));
		for (m=n; m<len && (m-n)<(sizeof(vars)-1); m++) {
			if (varbuf[m] == '\n')
				break;
			vars[m-n] = varbuf[m];
		}
		printf("%s\n", vars);
	}
}

/*
 * ProcessVars:Takes a buffer of "<var>=<value>\n" lines read from a file and ending in a NUL.
 * also accepts nvram files which are already in the format of <var1>=<value>\0\<var2>=<value2>\0
 * Removes carriage returns, empty lines, comment lines, and converts newlines to NULs.
 * Shortens buffer as needed and pads with NULs.  End of buffer is marked by two NULs.
*/

unsigned int
process_nvram_vars(char *varbuf, unsigned int len)
{
	char *dp;
	bool findNewline;
	int column;
	unsigned int buf_len, n;
	unsigned int pad = 0;

	dp = varbuf;

	findNewline = FALSE;
	column = 0;

	dump_nvram(varbuf, 0, 0, len);
	for (n = 0; n < len; n++) {
		if (varbuf[n] == '\r')
			continue;
		if (findNewline && varbuf[n] != '\n')
			continue;
		findNewline = FALSE;
		if (varbuf[n] == '#') {
			findNewline = TRUE;
			continue;
		}
		if (varbuf[n] == '\n') {
			if (column == 0)
				continue;
			*dp++ = 0;
			column = 0;
			continue;
		}
		dump_nvram(varbuf, column, n, len);
		*dp++ = varbuf[n];
		column++;
	}
	buf_len = (unsigned int)(dp - varbuf);
	if (buf_len % 4) {
		pad = 4 - buf_len % 4;
		if (pad && (buf_len + pad <= len)) {
			buf_len += pad;
		}
	}

	while (dp < varbuf + n)
		*dp++ = 0;

	return buf_len;
}

#ifndef setbit /* As in the header file */
#ifdef BCMUTILS_BIT_MACROS_USE_FUNCS
/* Set bit in byte array. */
void
setbit(void *array, uint bit)
{
	((uint8 *)array)[bit / NBBY] |= 1 << (bit % NBBY);
}

/* Clear bit in byte array. */
void
clrbit(void *array, uint bit)
{
	((uint8 *)array)[bit / NBBY] &= ~(1 << (bit % NBBY));
}

/* Test if bit is set in byte array. */
bool
isset(const void *array, uint bit)
{
	return (((const uint8 *)array)[bit / NBBY] & (1 << (bit % NBBY)));
}

/* Test if bit is clear in byte array. */
bool
isclr(const void *array, uint bit)
{
	return ((((const uint8 *)array)[bit / NBBY] & (1 << (bit % NBBY))) == 0);
}
#endif /* BCMUTILS_BIT_MACROS_USE_FUNCS */
#endif /* setbit */

void
BCMPOSTTRAPFN(set_bitrange)(void *array, uint start, uint end, uint maxbit)
{
	uint startbyte = start/NBBY;
	uint endbyte = end/NBBY;
	uint i, startbytelastbit, endbytestartbit;

	if (end >= start) {
		if (endbyte - startbyte > 1) {
			startbytelastbit = ((startbyte + 1) * NBBY) - 1;
			endbytestartbit = endbyte * NBBY;
			for (i = startbyte + 1; i < endbyte; i++)
				((uint8 *)array)[i] = 0xFF;
			for (i = start; i <= startbytelastbit; i++)
				setbit(array, i);
			for (i = endbytestartbit; i <= end; i++)
				setbit(array, i);
		} else {
			for (i = start; i <= end; i++)
				setbit(array, i);
		}
	} else {
		set_bitrange(array, start, maxbit, maxbit);
		set_bitrange(array, 0, end, maxbit);
	}
}

void
clr_bitrange(void *array, uint start, uint end, uint maxbit)
{
	uint startbyte = start/NBBY;
	uint endbyte = end/NBBY;
	uint i, startbytelastbit, endbytestartbit;

	if (end >= start) {
		if (endbyte - startbyte > 1) {
			startbytelastbit = ((startbyte + 1) * NBBY) - 1;
			endbytestartbit = endbyte * NBBY;
			for (i = startbyte + 1; i < endbyte; i++)
				((uint8 *)array)[i] = 0x0;
			for (i = start; i <= startbytelastbit; i++)
				clrbit(array, i);
			for (i = endbytestartbit; i <= end; i++)
				clrbit(array, i);
		} else {
			for (i = start; i <= end; i++)
				clrbit(array, i);
		}
	} else {
		clr_bitrange(array, start, maxbit, maxbit);
		clr_bitrange(array, 0, end, maxbit);
	}
}

/*
 * This api (set_bitrange_int_access) as same as set_bitrange but uses int32 operation
 * This api can be used in the place of set_bitrange but array should be word (32bit) alligned.
 * This api has to be used when the memory being accessed has restrictions of
 * not using them in 8bit (byte) mode and needing 32bit (word) mode.
 */
void
set_bitrange_u32(void *array, uint start, uint end, uint maxbit)
{
	uint startword = start/SIZE_BITS32(uint32);
	uint endword = end/SIZE_BITS32(uint32);
	uint startwordstartbit = start % SIZE_BITS32(uint32);
	uint endwordlastbit = end % SIZE_BITS32(uint32);
	/* Used to caluculate bit number from MSB */
	uint u32msbnum = SIZE_BITS32(uint32) - 1U;
	uint i;
	uint32 setbitsword;
	uint32 u32max = ~0U;

	ASSERT(ISALIGNED(array, sizeof(uint32))); /* array should be alligned for this API */

	if (start > end) {
		set_bitrange_u32(array, start, maxbit, maxbit);
		set_bitrange_u32(array, 0U, end, maxbit);
		return;
	}

	if (endword - startword) {
		/* Setting MSB bits including startwordstartbit */
		setbitsword = u32max << startwordstartbit;
		((uint32 *)array)[startword] |= setbitsword;

		/* Setting all bits in 'startword + 1' to 'endword - 1' */
		for (i = startword + 1U; i <= endword - 1U; i++) {
			((uint32 *)array)[i] = u32max;
		}

		/* Setting LSB bits including endwordlastbit */
		setbitsword = u32max >> (u32msbnum - endwordlastbit);
		((uint32 *)array)[endword] |= setbitsword;
	} else { /* start and end are in same word */
		/* Setting start bit to end bit including start and end bits */
		setbitsword =
			(u32max << startwordstartbit) & (u32max >> (u32msbnum - endwordlastbit));
		((uint32 *)array)[startword] |= setbitsword;
	}
}

/*
 * This api (clr_bitrange_u32) as same as clr_bitrange but uses int32 operation
 * This api can be used in the place of clr_bitrange but array should be word (32bit) alligned.
 * This api has to be used when the memory being accessed has restrictions of
 * not using them in 8bit (byte) mode and needing 32bit (word) mode.
 */
void
clr_bitrange_u32(void *array, uint start, uint end, uint maxbit)
{
	uint startword = start/SIZE_BITS32(uint32);
	uint endword = end/SIZE_BITS32(uint32);
	uint startwordstartbit = start % SIZE_BITS32(uint32);
	uint endwordlastbit = end % SIZE_BITS32(uint32);
	/* Used to caluculate bit number from MSB */
	uint u32msbnum = SIZE_BITS32(uint32) - 1U;
	uint i;
	uint32 clrbitsword;
	uint32 u32max = ~0U;

	ASSERT(ISALIGNED(array, sizeof(uint32))); /* array should be alligned for this API */

	if (start > end) {
		clr_bitrange_u32(array, start, maxbit, maxbit);
		clr_bitrange_u32(array, 0U, end, maxbit);
		return;
	}

	if (endword - startword) {
		/* Clearing MSB bits including startwordstartbit */
		clrbitsword = ~(u32max << startwordstartbit);
		((uint32 *)array)[startword] &= clrbitsword;

		/* Clearing all bits in 'startword + 1' to 'endword - 1' */
		for (i = startword + 1U; i <= endword - 1U; i++) {
			((uint32 *)array)[i] = 0U;
		}

		/* Clearing LSB bits including endwordlastbit */
		clrbitsword = ~(u32max >> (u32msbnum - endwordlastbit));
		((uint32 *)array)[endword] &= clrbitsword;
	} else { /* start and end are in same word */
		/* Clearing start bit to end bit including start and end bits */
		clrbitsword =
			~(u32max << startwordstartbit) | ~(u32max >> (u32msbnum - endwordlastbit));
		((uint32 *)array)[startword] &= clrbitsword;
	}
}

void
bcm_bitprint32(const uint32 u32arg)
{
	int i;
	for (i = NBITS(uint32) - 1; i >= 0; i--) {
		if (isbitset(u32arg, i)) {
			printf("1");
		} else {
			printf("0");
		}

		if ((i % NBBY) == 0) printf(" ");
	}
	printf("\n");
}

/* calculate checksum for ip header, tcp / udp header / data */
uint16
bcm_ip_cksum(uint8 *buf, uint32 len, uint32 sum)
{
	while (len > 1) {
		sum += (buf[0] << 8) | buf[1];
		buf += 2;
		len -= 2;
	}

	if (len > 0) {
		sum += (*buf) << 8;
	}

	while (sum >> 16) {
		sum = (sum & 0xffff) + (sum >> 16);
	}

	return ((uint16)~sum);
}

/* calculate a + b where a is a 64 bit number and b is a 32 bit number */
void
bcm_add_64(uint32* r_hi, uint32* r_lo, uint32 offset)
{
	uint32 r1_lo = *r_lo;
	(*r_lo) += offset;
	if (*r_lo < r1_lo)
		(*r_hi) ++;
}

/* calculate a - b where a is a 64 bit number and b is a 32 bit number */
void
bcm_sub_64(uint32* r_hi, uint32* r_lo, uint32 offset)
{
	uint32 r1_lo = *r_lo;
	(*r_lo) -= offset;
	if (*r_lo > r1_lo)
		(*r_hi) --;
}

int
BCMRAMFN(valid_bcmerror)(int e)
{
	return ((e <= 0) && (e >= BCME_LAST));
}

#ifdef DEBUG_COUNTER
#if (OSL_SYSUPTIME_SUPPORT == TRUE)
void counter_printlog(counter_tbl_t *ctr_tbl)
{
	uint32 now;

	if (!ctr_tbl->enabled)
		return;

	now = OSL_SYSUPTIME();

	if (now - ctr_tbl->prev_log_print > ctr_tbl->log_print_interval) {
		uint8 i = 0;
		printf("counter_print(%s %d):", ctr_tbl->name, now - ctr_tbl->prev_log_print);

		for (i = 0; i < ctr_tbl->needed_cnt; i++) {
			printf(" %u", ctr_tbl->cnt[i]);
		}
		printf("\n");

		ctr_tbl->prev_log_print = now;
		bzero(ctr_tbl->cnt, CNTR_TBL_MAX * sizeof(uint));
	}
}
#else
/* OSL_SYSUPTIME is not supported so no way to get time */
#define counter_printlog(a) do {} while (0)
#endif /* OSL_SYSUPTIME_SUPPORT == TRUE */
#endif /* DEBUG_COUNTER */

/* calculate partial checksum */
static uint32
ip_cksum_partial(uint32 sum, uint8 *val8, uint32 count)
{
	uint32 i;
	uint16 *val16 = (uint16 *)val8;

	ASSERT(val8 != NULL);
	/* partial chksum calculated on 16-bit values */
	ASSERT((count % 2) == 0);

	count /= 2;

	for (i = 0; i < count; i++) {
		sum += *val16++;
	}
	return sum;
}

/* calculate IP checksum */
static uint16
ip_cksum(uint32 sum, uint8 *val8, uint32 count)
{
	uint16 *val16 = (uint16 *)val8;

	ASSERT(val8 != NULL);

	while (count > 1) {
		sum += *val16++;
		count -= 2;
	}
	/*  add left-over byte, if any */
	if (count > 0) {
		sum += (*(uint8 *)val16);
	}

	/*  fold 32-bit sum to 16 bits */
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return ((uint16)~sum);
}

/* calculate IPv4 header checksum
 * - input ip points to IP header in network order
 * - output cksum is in network order
 */
uint16
ipv4_hdr_cksum(uint8 *ip, uint ip_len)
{
	uint32 sum = 0;
	uint8 *ptr = ip;

	ASSERT(ip != NULL);
	ASSERT(ip_len >= IPV4_MIN_HEADER_LEN);

	if (ip_len < IPV4_MIN_HEADER_LEN) {
		return 0;
	}

	/* partial cksum skipping the hdr_chksum field */
	sum = ip_cksum_partial(sum, ptr, OFFSETOF(struct ipv4_hdr, hdr_chksum));
	ptr += OFFSETOF(struct ipv4_hdr, hdr_chksum) + 2;

	/* return calculated chksum */
	return ip_cksum(sum, ptr, ip_len - OFFSETOF(struct ipv4_hdr, src_ip));
}

/* calculate TCP header checksum using partial sum */
static uint16
tcp_hdr_chksum(uint32 sum, uint8 *tcp_hdr, uint16 tcp_len)
{
	uint8 *ptr = tcp_hdr;

	ASSERT(tcp_hdr != NULL);
	ASSERT(tcp_len >= TCP_MIN_HEADER_LEN);

	/* partial TCP cksum skipping the chksum field */
	sum = ip_cksum_partial(sum, ptr, OFFSETOF(struct bcmtcp_hdr, chksum));
	ptr += OFFSETOF(struct bcmtcp_hdr, chksum) + 2;

	/* return calculated chksum */
	return ip_cksum(sum, ptr, tcp_len - OFFSETOF(struct bcmtcp_hdr, urg_ptr));
}

struct tcp_pseudo_hdr {
	uint8   src_ip[IPV4_ADDR_LEN];  /* Source IP Address */
	uint8   dst_ip[IPV4_ADDR_LEN];  /* Destination IP Address */
	uint8	zero;
	uint8	prot;
	uint16	tcp_size;
};

/* calculate IPv4 TCP header checksum
 * - input ip and tcp points to IP and TCP header in network order
 * - output cksum is in network order
 */
uint16
ipv4_tcp_hdr_cksum(uint8 *ip, uint8 *tcp, uint16 tcp_len)
{
	struct ipv4_hdr *ip_hdr = (struct ipv4_hdr *)ip;
	struct tcp_pseudo_hdr tcp_ps;
	uint32 sum = 0;

	ASSERT(ip != NULL);
	ASSERT(tcp != NULL);
	ASSERT(tcp_len >= TCP_MIN_HEADER_LEN);

	/* pseudo header cksum */
	memset(&tcp_ps, 0, sizeof(tcp_ps));
	memcpy(&tcp_ps.dst_ip, ip_hdr->dst_ip, IPV4_ADDR_LEN);
	memcpy(&tcp_ps.src_ip, ip_hdr->src_ip, IPV4_ADDR_LEN);
	tcp_ps.zero = 0;
	tcp_ps.prot = ip_hdr->prot;
	tcp_ps.tcp_size = hton16(tcp_len);
	sum = ip_cksum_partial(sum, (uint8 *)&tcp_ps, sizeof(tcp_ps));

	/* return calculated TCP header chksum */
	return tcp_hdr_chksum(sum, tcp, tcp_len);
}

struct ipv6_pseudo_hdr {
	uint8  saddr[IPV6_ADDR_LEN];
	uint8  daddr[IPV6_ADDR_LEN];
	uint16 payload_len;
	uint8  zero;
	uint8  next_hdr;
};

/* calculate IPv6 TCP header checksum
 * - input ipv6 and tcp points to IPv6 and TCP header in network order
 * - output cksum is in network order
 */
uint16
ipv6_tcp_hdr_cksum(uint8 *ipv6, uint8 *tcp, uint16 tcp_len)
{
	struct ipv6_hdr *ipv6_hdr = (struct ipv6_hdr *)ipv6;
	struct ipv6_pseudo_hdr ipv6_pseudo;
	uint32 sum = 0;

	ASSERT(ipv6 != NULL);
	ASSERT(tcp != NULL);
	ASSERT(tcp_len >= TCP_MIN_HEADER_LEN);

	/* pseudo header cksum */
	memset((char *)&ipv6_pseudo, 0, sizeof(ipv6_pseudo));
	memcpy((char *)ipv6_pseudo.saddr, (char *)ipv6_hdr->saddr.addr,
		sizeof(ipv6_pseudo.saddr));
	memcpy((char *)ipv6_pseudo.daddr, (char *)ipv6_hdr->daddr.addr,
		sizeof(ipv6_pseudo.daddr));
	ipv6_pseudo.payload_len = ipv6_hdr->payload_len;
	ipv6_pseudo.next_hdr = ipv6_hdr->nexthdr;
	sum = ip_cksum_partial(sum, (uint8 *)&ipv6_pseudo, sizeof(ipv6_pseudo));

	/* return calculated TCP header chksum */
	return tcp_hdr_chksum(sum, tcp, tcp_len);
}

void *_bcmutils_dummy_fn = NULL;

/* GROUP 1 --- start
 * These function under GROUP 1 are general purpose functions to do complex number
 * calculations and square root calculation.
 */

uint32 sqrt_int(uint32 value)
{
	uint32 root = 0, shift = 0;

	/* Compute integer nearest to square root of input integer value */
	for (shift = 0; shift < 32; shift += 2) {
		if (((0x40000000 >> shift) + root) <= value) {
			value -= ((0x40000000 >> shift) + root);
			root = (root >> 1) | (0x40000000 >> shift);
		}
		else {
			root = root >> 1;
		}
	}

	/* round to the nearest integer */
	if (root < value) ++root;

	return root;
}
/* GROUP 1 --- end */

/* read/write field in a consecutive bits in an octet array.
 * 'addr' is the octet array's start byte address
 * 'size' is the octet array's byte size
 * 'stbit' is the value's start bit offset
 * 'nbits' is the value's bit size
 * This set of utilities are for convenience. Don't use them
 * in time critical/data path as there's a great overhead in them.
 */
void
setbits(uint8 *addr, uint size, uint stbit, uint nbits, uint32 val)
{
	uint fbyte = stbit >> 3;		/* first byte */
	uint lbyte = (stbit + nbits - 1) >> 3;	/* last byte */
	uint fbit = stbit & 7;			/* first bit in the first byte */
	uint rbits = (nbits > 8 - fbit ?
	              nbits - (8 - fbit) :
	              0) & 7;			/* remaining bits of the last byte when not 0 */
	uint8 mask;
	uint byte;

	BCM_REFERENCE(size);

	ASSERT(fbyte < size);
	ASSERT(lbyte < size);
	ASSERT(nbits <= (sizeof(val) << 3));

	/* all bits are in the same byte */
	if (fbyte == lbyte) {
		mask = ((1 << nbits) - 1) << fbit;
		addr[fbyte] &= ~mask;
		addr[fbyte] |= (uint8)(val << fbit);
		return;
	}

	/* first partial byte */
	if (fbit > 0) {
		mask = (0xff << fbit);
		addr[fbyte] &= ~mask;
		addr[fbyte] |= (uint8)(val << fbit);
		val >>= (8 - fbit);
		nbits -= (8 - fbit);
		fbyte ++;	/* first full byte */
	}

	/* last partial byte */
	if (rbits > 0) {
		mask = (1 << rbits) - 1;
		addr[lbyte] &= ~mask;
		addr[lbyte] |= (uint8)(val >> (nbits - rbits));
		lbyte --;	/* last full byte */
	}

	/* remaining full byte(s) */
	for (byte = fbyte; byte <= lbyte; byte ++) {
		addr[byte] = (uint8)val;
		val >>= 8;
	}
}

uint32
getbits(const uint8 *addr, uint size, uint stbit, uint nbits)
{
	uint fbyte = stbit >> 3;		/* first byte */
	uint lbyte = (stbit + nbits - 1) >> 3;	/* last byte */
	uint fbit = stbit & 7;			/* first bit in the first byte */
	uint rbits = (nbits > 8 - fbit ?
	              nbits - (8 - fbit) :
	              0) & 7;			/* remaining bits of the last byte when not 0 */
	uint32 val = 0;
	uint bits = 0;				/* bits in first partial byte */
	uint8 mask;
	uint byte;

	BCM_REFERENCE(size);

	ASSERT(fbyte < size);
	ASSERT(lbyte < size);
	ASSERT(nbits <= (sizeof(val) << 3));

	/* all bits are in the same byte */
	if (fbyte == lbyte) {
		mask = ((1 << nbits) - 1) << fbit;
		val = (addr[fbyte] & mask) >> fbit;
		return val;
	}

	/* first partial byte */
	if (fbit > 0) {
		bits = 8 - fbit;
		mask = (0xff << fbit);
		val |= (addr[fbyte] & mask) >> fbit;
		fbyte ++;	/* first full byte */
	}

	/* last partial byte */
	if (rbits > 0) {
		mask = (1 << rbits) - 1;
		val |= (addr[lbyte] & mask) << (nbits - rbits);
		lbyte --;	/* last full byte */
	}

	/* remaining full byte(s) */
	for (byte = fbyte; byte <= lbyte; byte ++) {
		val |= (addr[byte] << (((byte - fbyte) << 3) + bits));
	}

	return val;
}

#if defined(BCMDBG) || defined(WLMSG_ASSOC)
/* support for getting 802.11 frame type/name based on frame kind */
#define FK_NAME_DECL(x) {FC_##x, #x}
static const struct {
    uint fk;
    const char *name;
} bcm_80211_fk_names[] =  {
	FK_NAME_DECL(ASSOC_REQ),
	FK_NAME_DECL(ASSOC_RESP),
	FK_NAME_DECL(REASSOC_REQ),
	FK_NAME_DECL(REASSOC_RESP),
	FK_NAME_DECL(PROBE_REQ),
	FK_NAME_DECL(PROBE_RESP),
	FK_NAME_DECL(BEACON),
	FK_NAME_DECL(ATIM),
	FK_NAME_DECL(DISASSOC),
	FK_NAME_DECL(AUTH),
	FK_NAME_DECL(DEAUTH),
	FK_NAME_DECL(ACTION),
	FK_NAME_DECL(ACTION_NOACK),
	FK_NAME_DECL(CTL_TRIGGER),
	FK_NAME_DECL(CTL_WRAPPER),
	FK_NAME_DECL(BLOCKACK_REQ),
	FK_NAME_DECL(BLOCKACK),
	FK_NAME_DECL(PS_POLL),
	FK_NAME_DECL(RTS),
	FK_NAME_DECL(CTS),
	FK_NAME_DECL(ACK),
	FK_NAME_DECL(CF_END),
	FK_NAME_DECL(CF_END_ACK),
	FK_NAME_DECL(DATA),
	FK_NAME_DECL(NULL_DATA),
	FK_NAME_DECL(DATA_CF_ACK),
	FK_NAME_DECL(QOS_DATA),
	FK_NAME_DECL(QOS_NULL)
};
static const uint n_bcm_80211_fk_names = ARRAYSIZE(bcm_80211_fk_names);

const char *bcm_80211_fk_name(uint fk)
{
	uint i;
	for (i = 0; i < n_bcm_80211_fk_names; ++i) {
		if (bcm_80211_fk_names[i].fk == fk) {
			return bcm_80211_fk_names[i].name;
		}
	}
	return "unknown";
}
#endif /* BCMDBG || WLMSG_ASSOC */

#ifdef BCMDRIVER

/** allocate variable sized data with 'size' bytes. note: vld should NOT be null.
 */
int
bcm_vdata_alloc(osl_t *osh, var_len_data_t *vld, uint32 size)
{
	int ret = BCME_ERROR;
	uint8 *dat = NULL;

	if (vld == NULL) {
		ASSERT(0);
		goto done;
	}

	/* trying to allocate twice? */
	if (vld->vdata != NULL) {
		ASSERT(0);
		goto done;
	}

	/* trying to allocate 0 size? */
	if (size == 0) {
		ASSERT(0);
		ret = BCME_BADARG;
		goto done;
	}

	dat = MALLOCZ(osh, size);
	if (dat == NULL) {
		ret = BCME_NOMEM;
		goto done;
	}
	vld->vlen = size;
	vld->vdata = dat;
	ret = BCME_OK;
done:
	return ret;
}

/** free memory associated with variable sized data. note: vld should NOT be null.
 */
int
bcm_vdata_free(osl_t *osh, var_len_data_t *vld)
{
	int ret = BCME_ERROR;

	if (vld == NULL) {
		ASSERT(0);
		goto done;
	}

	if (vld->vdata) {
		MFREE(osh, vld->vdata, vld->vlen);
		vld->vlen = 0;
		ret = BCME_OK;
	}
done:
	return ret;
}

/* return TRUE if :
 * - both buffers are of length 0
 * OR
 * - both buffers are NULL
 * OR
 * lengths and contents are the same.
 */
bool
bcm_match_buffers(const uint8 *b1, uint b1_len, const uint8 *b2, uint b2_len)

{
	if (b1_len == 0 && b2_len == 0) {
		return TRUE;
	}

	if (b1 == NULL && b2 == NULL) {
		return TRUE;
	}

	/* If they are not both NULL, neither can be */
	if (b1 == NULL || b2 == NULL) {
		return FALSE;
	}

	if ((b1_len == b2_len) && !memcmp(b1, b2, b1_len)) {
		return TRUE;
	}
	return FALSE;
}

#ifdef PRIVACY_MASK
/* applies privacy mask on the input address itself */
void
BCMRAMFN(bcm_ether_privacy_mask)(struct ether_addr *addr)
{
	struct ether_addr *privacy = privacy_addrmask_get();
	if (addr && !ETHER_ISMULTI(addr)) {
		*(uint32*)(&(addr->octet[0])) &= *((uint32*)&privacy->octet[0]);
		*(uint16*)(&(addr->octet[4])) &= *((uint16*)&privacy->octet[4]);
	}
}
#endif /* PRIVACY_MASK */
#endif /* BCMDRIVER */

/* Count the number of elements not matching a given value in a null terminated array */
int
BCMATTACHFN(array_value_mismatch_count)(uint8 value, uint8 *array, int array_size)
{
	int i;
	int count = 0;

	for (i = 0; i < array_size; i++) {
		/* exit if a null terminator is found */
		if (array[i] == 0) {
			break;
		}
		if (array[i] != value) {
			count++;
		}
	}
	return count;
}

/* Count the number of non-zero elements in an uint8 array */
int
BCMATTACHFN(array_nonzero_count)(uint8 *array, int array_size)
{
	return array_value_mismatch_count(0, array, array_size);
}

/* Count the number of non-zero elements in an int16 array */
int
BCMATTACHFN(array_nonzero_count_int16)(int16 *array, int array_size)
{
	int i;
	int count = 0;

	for (i = 0; i < array_size; i++) {
		if (array[i] != 0) {
			count++;
		}
	}
	return count;
}

/* Count the number of zero elements in an uint8 array */
int
BCMATTACHFN(array_zero_count)(uint8 *array, int array_size)
{
	int i;
	int count = 0;

	for (i = 0; i < array_size; i++) {
		if (array[i] == 0) {
			count++;
		}
	}
	return count;
}

/* Validate an array that can be 1 of 2 data types.
 * One of array1 or array2 should be non-NULL.  The other should be NULL.
 */
static int
BCMATTACHFN(verify_ordered_array)(uint8 *array1, int16 *array2, int array_size,
	int range_lo, int range_hi, bool err_if_no_zero_term, bool is_ordered)
{
	int ret;
	int i;
	int val = 0;
	int prev_val = 0;

	ret = err_if_no_zero_term ? BCME_NOTFOUND : BCME_OK;

	/* Check that:
	 * - values are in descending order.
	 * - values are within the valid range.
	 */
	for (i = 0; i < array_size; i++) {
		if (array1) {
			val = (int)array1[i];
		} else if (array2) {
			val = (int)array2[i];
		} else {
			/* both array parameters are NULL */
			return BCME_NOTFOUND;
		}
		if (val == 0) {
			/* array is zero-terminated */
			ret = BCME_OK;
			break;
		}

		if (is_ordered && i > 0 && val > prev_val) {
			/* array is not in descending order */
			ret = BCME_BADOPTION;
			break;
		}
		prev_val = val;

		if (val < range_lo || val > range_hi) {
			/* array value out of range */
			ret = BCME_RANGE;
			break;
		}
	}

	return ret;
}

/* Validate an ordered uint8 configuration array */
int
BCMATTACHFN(verify_ordered_array_uint8)(uint8 *array, int array_size,
	uint8 range_lo, uint8 range_hi)
{
	return verify_ordered_array(array, NULL, array_size, (int)range_lo, (int)range_hi,
		TRUE, TRUE);
}

/* Validate an ordered int16 non-zero-terminated configuration array */
int
BCMATTACHFN(verify_ordered_array_int16)(int16 *array, int array_size,
	int16 range_lo, int16 range_hi)
{
	return verify_ordered_array(NULL, array, array_size, (int)range_lo, (int)range_hi,
		FALSE, TRUE);
}

/* Validate all values in an array are in range */
int
BCMATTACHFN(verify_array_values)(uint8 *array, int array_size,
	int range_lo, int range_hi, bool zero_terminated)
{
	int ret = BCME_OK;
	int i;
	int val = 0;

	/* Check that:
	 * - values are in strict descending order.
	 * - values are within the valid range.
	 */
	for (i = 0; i < array_size; i++) {
		val = (int)array[i];
		if (val == 0 && zero_terminated) {
			ret = BCME_OK;
			break;
		}
		if (val < range_lo || val > range_hi) {
			/* array value out of range */
			ret = BCME_RANGE;
			break;
		}
	}
	return ret;
}

/* Adds/replaces NVRAM variable with given value
 * varbuf[in,out]   - Buffer with NVRAM variables (sequence of zero-terminated 'name=value' records,
 *                    terminated with additional zero)
 * buflen[in]       - Length of buffer (may, even should, have some unused space)
 * variable[in]     - Variable to add/replace in 'name=value' form
 * datalen[out,opt] - Optional output parameter - resulting length of data in buffer
 * Returns TRUE on success, FALSE if buffer too short or variable specified incorrectly
 */
bool
replace_nvram_variable(char *varbuf, unsigned int buflen, const char *variable,
	unsigned int *datalen)
{
	char *p;
	int variable_heading_len, record_len, variable_record_len = (int)strlen(variable) + 1;
	char *buf_end = varbuf + buflen;
	p = strchr(variable, '=');
	if (!p) {
		return FALSE;
	}
	/* Length of given variable name, followed by '=' */
	variable_heading_len = (int)((const char *)(p + 1) - variable);
	/* Scanning NVRAM, record by record up to trailing 0 */
	for (p = varbuf; *p; p += strlen(p) + 1) {
		/* If given variable found - remove it */
		if (!strncmp(p, variable, variable_heading_len)) {
			record_len = (int)strlen(p) + 1;
			memmove_s(p, buf_end - p, p + record_len, buf_end - (p + record_len));
		}
	}
	/* If buffer does not have space for given variable - return FALSE */
	if ((p + variable_record_len + 1) > buf_end) {
		return FALSE;
	}
	/* Copy given variable to end of buffer */
	memmove_s(p, buf_end - p, variable, variable_record_len);
	/* Adding trailing 0 */
	p[variable_record_len] = 0;
	/* Setting optional output parameter - length of data in buffer */
	if (datalen) {
		*datalen = (unsigned int)(p + variable_record_len + 1  - varbuf);
	}
	return TRUE;
}

/*
 * Gets the ceil bit set to the nearest power of 2
 * val[in]	- value for which nearest power of 2 bit set to be returned
 * bitpos[out]	- the position of the nearest power of 2 bit set
 */
uint8
bcm_get_ceil_pow_2(uint val)
{
	uint8 bitpos = 0;
	ASSERT(val);
	if (val & (val-1)) {
		/* val is not powers of 2.
		 * pad it, so that allocation will be aligned to
		 * next immediate powers of 2.
		 */
		bitpos = 1;
	}
	while (val >>= 1) {
		bitpos ++;
	}
	return (bitpos);
}

#if !defined(BCMDONGLEHOST)
/** Initialization of varbuf structure */
void
BCMATTACHFN(varbuf_init)(varbuf_t *b, char *buf, uint size)
{
	b->size = size;
	b->base = b->buf = buf;
}

/** append a null terminated var=value string */
int
BCMATTACHFN(varbuf_append)(varbuf_t *b, const char *fmt, ...)
{
	va_list ap;
	int r;
	size_t len;
	char *s;

	if (b->size < 2)
	  return 0;

	va_start(ap, fmt);
	r = vsnprintf(b->buf, b->size, fmt, ap);
	va_end(ap);

	/* C99 snprintf behavior returns r >= size on overflow,
	 * others return -1 on overflow.
	 * All return -1 on format error.
	 * We need to leave room for 2 null terminations, one for the current var
	 * string, and one for final null of the var table. So check that the
	 * strlen written, r, leaves room for 2 chars.
	 */
	if ((r == -1) || (r > (int)(b->size - 2))) {
		b->size = 0;
		return 0;
	}

	/* Remove any earlier occurrence of the same variable */
	if ((s = strchr(b->buf, '=')) != NULL) {
		len = (size_t)(s - b->buf);
		for (s = b->base; s < b->buf;) {
			if ((memcmp(s, b->buf, len) == 0) && s[len] == '=') {
				len = strlen(s) + 1;
				memmove(s, (s + len), ((b->buf + r + 1) - (s + len)));
				b->buf -= len;
				b->size += (unsigned int)len;
				break;
			}

			while (*s++)
				;
		}
	}

	/* skip over this string's null termination */
	r++;
	b->size -= r;
	b->buf += r;

	return r;
}

#if defined(BCMDRIVER)
/**
 * Create variable table from memory.
 * Return 0 on success, nonzero on error.
 */
int
BCMATTACHFN(initvars_table)(osl_t *osh, char *start, char *end, char **vars,
	uint *count)
{
	int c = (int)(end - start);

	/* do it only when there is more than just the null string */
	if (c > 1) {
		char *vp = MALLOC(osh, c);
		ASSERT(vp != NULL);
		if (!vp)
			return BCME_NOMEM;
		bcopy(start, vp, c);
		*vars = vp;
		*count = c;
	}
	else {
		*vars = NULL;
		*count = 0;
	}

	return 0;
}
#endif /* BCMDRIVER */

#endif /* !BCMDONGLEHOST */

/* bit shift operation in serialized buffer taking input bits % 8 */
int buf_shift_right(uint8 *buf, uint16 len, uint8 bits)
{
	uint16 i;

	if (len == 0 || (bits == 0) || (bits >= NBBY)) {
		return BCME_BADARG;
	}

	for (i = len - 1u; i > 0; i--) {
		buf[i] = (buf[i - 1u] << (NBBY - bits)) | (buf[i] >> bits);
	}
	buf[0] >>= bits;

	return BCME_OK;
}

/* print the content of the 'buf' in hex string format */
void
prhexstr(const char *prefix, const uint8 *buf, uint len, bool newline)
{
	if (len > 0) {
		uint i;

		if (prefix != NULL) {
			printf("%s", prefix);
		}
		for (i = 0; i < len; i ++) {
			printf("%02X", buf[i]);
		}
		if (newline) {
			printf("\n");
		}
	}
}

/* Add to adjust the 802.1x priority */
void
pktset8021xprio(void *pkt, int prio)
{
	struct ether_header *eh;
	uint8 *pktdata;
	if(prio == PKTPRIO(pkt))
		return;
	pktdata = (uint8 *)PKTDATA(OSH_NULL, pkt);
	ASSERT(ISALIGNED((uintptr)pktdata, sizeof(uint16)));
	eh = (struct ether_header *) pktdata;
	if (eh->ether_type == hton16(ETHER_TYPE_802_1X)) {
		ASSERT(prio >= 0 && prio <= MAXPRIO);
		PKTSETPRIO(pkt, prio);
	}
}
