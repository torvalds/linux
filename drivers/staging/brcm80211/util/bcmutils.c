/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <stdarg.h>
#include <osl.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linuxver.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmnvram.h>
#include <bcmendian.h>
#include <bcmdevs.h>
#include <proto/ethernet.h>
#include <proto/802.1d.h>
#include <proto/802.11.h>

#ifdef WLC_LOW
/* nvram vars cache */
static char *nvram_vars;
static int vars_len = -1;
#endif				/* WLC_LOW */

/* copy a pkt buffer chain into a buffer */
uint pktcopy(osl_t *osh, void *p, uint offset, int len, uchar *buf)
{
	uint n, ret = 0;

	if (len < 0)
		len = 4096;	/* "infinite" */

	/* skip 'offset' bytes */
	for (; p && offset; p = PKTNEXT(p)) {
		if (offset < (uint) PKTLEN(p))
			break;
		offset -= PKTLEN(p);
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = PKTNEXT(p)) {
		n = MIN((uint) PKTLEN(p) - offset, (uint) len);
		bcopy(PKTDATA(p) + offset, buf, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}

/* copy a buffer into a pkt buffer chain */
uint pktfrombuf(osl_t *osh, void *p, uint offset, int len, uchar *buf)
{
	uint n, ret = 0;

	/* skip 'offset' bytes */
	for (; p && offset; p = PKTNEXT(p)) {
		if (offset < (uint) PKTLEN(p))
			break;
		offset -= PKTLEN(p);
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = PKTNEXT(p)) {
		n = MIN((uint) PKTLEN(p) - offset, (uint) len);
		bcopy(buf, PKTDATA(p) + offset, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}

/* return total length of buffer chain */
uint BCMFASTPATH pkttotlen(osl_t *osh, void *p)
{
	uint total;

	total = 0;
	for (; p; p = PKTNEXT(p))
		total += PKTLEN(p);
	return total;
}

/* return the last buffer of chained pkt */
void *pktlast(osl_t *osh, void *p)
{
	for (; PKTNEXT(p); p = PKTNEXT(p))
		;

	return p;
}

/* count segments of a chained packet */
uint BCMFASTPATH pktsegcnt(osl_t *osh, void *p)
{
	uint cnt;

	for (cnt = 0; p; p = PKTNEXT(p))
		cnt++;

	return cnt;
}

/*
 * osl multiple-precedence packet queue
 * hi_prec is always >= the number of the highest non-empty precedence
 */
void *BCMFASTPATH pktq_penq(struct pktq *pq, int prec, void *p)
{
	struct pktq_prec *q;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	ASSERT(PKTLINK(p) == NULL);	/* queueing chains not allowed */

	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));

	q = &pq->q[prec];

	if (q->head)
		PKTSETLINK(q->tail, p);
	else
		q->head = p;

	q->tail = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (u8) prec;

	return p;
}

void *BCMFASTPATH pktq_penq_head(struct pktq *pq, int prec, void *p)
{
	struct pktq_prec *q;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	ASSERT(PKTLINK(p) == NULL);	/* queueing chains not allowed */

	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));

	q = &pq->q[prec];

	if (q->head == NULL)
		q->tail = p;

	PKTSETLINK(p, q->head);
	q->head = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (u8) prec;

	return p;
}

void *BCMFASTPATH pktq_pdeq(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	void *p;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	q->head = PKTLINK(p);
	if (q->head == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}

void *BCMFASTPATH pktq_pdeq_tail(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	void *p, *prev;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = PKTLINK(p))
		prev = p;

	if (prev)
		PKTSETLINK(prev, NULL);
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	return p;
}

void
pktq_pflush(osl_t *osh, struct pktq *pq, int prec, bool dir, ifpkt_cb_t fn,
	    int arg)
{
	struct pktq_prec *q;
	void *p, *prev = NULL;

	q = &pq->q[prec];
	p = q->head;
	while (p) {
		if (fn == NULL || (*fn) (p, arg)) {
			bool head = (p == q->head);
			if (head)
				q->head = PKTLINK(p);
			else
				PKTSETLINK(prev, PKTLINK(p));
			PKTSETLINK(p, NULL);
			PKTFREE(osh, p, dir);
			q->len--;
			pq->len--;
			p = (head ? q->head : PKTLINK(prev));
		} else {
			prev = p;
			p = PKTLINK(p);
		}
	}

	if (q->head == NULL) {
		ASSERT(q->len == 0);
		q->tail = NULL;
	}
}

bool BCMFASTPATH pktq_pdel(struct pktq *pq, void *pktbuf, int prec)
{
	struct pktq_prec *q;
	void *p;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	if (!pktbuf)
		return FALSE;

	q = &pq->q[prec];

	if (q->head == pktbuf) {
		q->head = PKTLINK(pktbuf);
		if (q->head == NULL)
			q->tail = NULL;
	} else {
		for (p = q->head; p && PKTLINK(p) != pktbuf; p = PKTLINK(p))
			;
		if (p == NULL)
			return FALSE;

		PKTSETLINK(p, PKTLINK(pktbuf));
		if (q->tail == pktbuf)
			q->tail = p;
	}

	q->len--;
	pq->len--;
	PKTSETLINK(pktbuf, NULL);
	return TRUE;
}

void pktq_init(struct pktq *pq, int num_prec, int max_len)
{
	int prec;

	ASSERT(num_prec > 0 && num_prec <= PKTQ_MAX_PREC);

	/* pq is variable size; only zero out what's requested */
	bzero(pq,
	      OFFSETOF(struct pktq, q) + (sizeof(struct pktq_prec) * num_prec));

	pq->num_prec = (uint16) num_prec;

	pq->max = (uint16) max_len;

	for (prec = 0; prec < num_prec; prec++)
		pq->q[prec].max = pq->max;
}

void *BCMFASTPATH pktq_deq(struct pktq *pq, int *prec_out)
{
	struct pktq_prec *q;
	void *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	q->head = PKTLINK(p);
	if (q->head == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	if (prec_out)
		*prec_out = prec;

	PKTSETLINK(p, NULL);

	return p;
}

void *BCMFASTPATH pktq_deq_tail(struct pktq *pq, int *prec_out)
{
	struct pktq_prec *q;
	void *p, *prev;
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = PKTLINK(p))
		prev = p;

	if (prev)
		PKTSETLINK(prev, NULL);
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	if (prec_out)
		*prec_out = prec;

	PKTSETLINK(p, NULL);

	return p;
}

void *pktq_peek(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	if (prec_out)
		*prec_out = prec;

	return pq->q[prec].head;
}

void *pktq_peek_tail(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	if (prec_out)
		*prec_out = prec;

	return pq->q[prec].tail;
}

void pktq_flush(osl_t *osh, struct pktq *pq, bool dir, ifpkt_cb_t fn, int arg)
{
	int prec;
	for (prec = 0; prec < pq->num_prec; prec++)
		pktq_pflush(osh, pq, prec, dir, fn, arg);
	if (fn == NULL)
		ASSERT(pq->len == 0);
}

/* Return sum of lengths of a specific set of precedences */
int pktq_mlen(struct pktq *pq, uint prec_bmp)
{
	int prec, len;

	len = 0;

	for (prec = 0; prec <= pq->hi_prec; prec++)
		if (prec_bmp & (1 << prec))
			len += pq->q[prec].len;

	return len;
}

/* Priority dequeue from a specific set of precedences */
void *BCMFASTPATH pktq_mdeq(struct pktq *pq, uint prec_bmp, int *prec_out)
{
	struct pktq_prec *q;
	void *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	while ((prec_bmp & (1 << prec)) == 0 || pq->q[prec].head == NULL)
		if (prec-- == 0)
			return NULL;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	q->head = PKTLINK(p);
	if (q->head == NULL)
		q->tail = NULL;

	q->len--;

	if (prec_out)
		*prec_out = prec;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}

/* parse a xx:xx:xx:xx:xx:xx format ethernet address */
int BCMROMFN(bcm_ether_atoe) (char *p, struct ether_addr *ea)
{
	int i = 0;

	for (;;) {
		ea->octet[i++] = (char)simple_strtoul(p, &p, 16);
		if (!*p++ || i == 6)
			break;
	}

	return i == 6;
}

char *bcm_ether_ntoa(const struct ether_addr *ea, char *buf)
{
	snprintf(buf, 18, "%pM", ea->octet);
	return buf;
}

void bcm_mdelay(uint ms)
{
	uint i;

	for (i = 0; i < ms; i++) {
		OSL_DELAY(1000);
	}
}

/*
 * Search the name=value vars for a specific one and return its value.
 * Returns NULL if not found.
 */
char *getvar(char *vars, const char *name)
{
	char *s;
	int len;

	if (!name)
		return NULL;

	len = strlen(name);
	if (len == 0)
		return NULL;

	/* first look in vars[] */
	for (s = vars; s && *s;) {
		if ((bcmp(s, name, len) == 0) && (s[len] == '='))
			return &s[len + 1];

		while (*s++)
			;
	}

	/* then query nvram */
	return nvram_get(name);
}

/*
 * Search the vars for a specific one and return its value as
 * an integer. Returns 0 if not found.
 */
int getintvar(char *vars, const char *name)
{
	char *val;

	val = getvar(vars, name);
	if (val == NULL)
		return 0;

	return simple_strtoul(val, NULL, 0);
}

int getintvararray(char *vars, const char *name, u8 index)
{
	char *buf, *endp;
	int i = 0;
	int val = 0;

	buf = getvar(vars, name);
	if (buf == NULL) {
		return 0;
	}

	/* table values are always separated by "," or " " */
	while (*buf != '\0') {
		val = simple_strtoul(buf, &endp, 0);
		if (i == index) {
			return val;
		}
		buf = endp;
		/* delimiter is ',' */
		if (*buf == ',')
			buf++;
		i++;
	}
	return 0;
}

/* Search for token in comma separated token-string */
static int findmatch(char *string, char *name)
{
	uint len;
	char *c;

	len = strlen(name);
	while ((c = strchr(string, ',')) != NULL) {
		if (len == (uint) (c - string) && !strncmp(string, name, len))
			return 1;
		string = c + 1;
	}

	return !strcmp(string, name);
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
uint getgpiopin(char *vars, char *pin_name, uint def_pin)
{
	char name[] = "gpioXXXX";
	char *val;
	uint pin;

	/* Go thru all possibilities till a match in pin name */
	for (pin = 0; pin < GPIO_NUMPINS; pin++) {
		snprintf(name, sizeof(name), "gpio%d", pin);
		val = getvar(vars, name);
		if (val && findmatch(val, pin_name))
			return pin;
	}

	if (def_pin != GPIO_PIN_NOTDEFINED) {
		/* make sure the default pin is not used by someone else */
		snprintf(name, sizeof(name), "gpio%d", def_pin);
		if (getvar(vars, name)) {
			def_pin = GPIO_PIN_NOTDEFINED;
		}
	}
	return def_pin;
}

#if defined(BCMDBG)
/* pretty hex print a pkt buffer chain */
void prpkt(const char *msg, osl_t *osh, void *p0)
{
	void *p;

	if (msg && (msg[0] != '\0'))
		printf("%s:\n", msg);

	for (p = p0; p; p = PKTNEXT(p))
		prhex(NULL, PKTDATA(p), PKTLEN(p));
}
#endif				/* defined(BCMDBG) */

static char bcm_undeferrstr[32];
static const char *bcmerrorstrtable[] = BCMERRSTRINGTABLE;

/* Convert the error codes into related error strings  */
const char *bcmerrorstr(int bcmerror)
{
	/* check if someone added a bcmerror code but forgot to add errorstring */
	ASSERT(ABS(BCME_LAST) == (ARRAYSIZE(bcmerrorstrtable) - 1));

	if (bcmerror > 0 || bcmerror < BCME_LAST) {
		snprintf(bcm_undeferrstr, sizeof(bcm_undeferrstr),
			 "Undefined error %d", bcmerror);
		return bcm_undeferrstr;
	}

	ASSERT(strlen(bcmerrorstrtable[-bcmerror]) < BCME_STRLEN);

	return bcmerrorstrtable[-bcmerror];
}

#ifdef WLC_LOW
static void BCMINITFN(bcm_nvram_refresh) (char *flash)
{
	int i;
	int ret = 0;

	ASSERT(flash != NULL);

	/* default "empty" vars cache */
	bzero(flash, 2);

	ret = nvram_getall(flash, NVRAM_SPACE);
	if (ret)
		return;

	/* determine nvram length */
	for (i = 0; i < NVRAM_SPACE; i++) {
		if (flash[i] == '\0' && flash[i + 1] == '\0')
			break;
	}

	if (i > 1)
		vars_len = i + 2;
	else
		vars_len = 0;
}

char *bcm_nvram_vars(uint *length)
{
#ifndef BCMNVRAMR
	/* cache may be stale if nvram is read/write */
	if (nvram_vars) {
		ASSERT(!bcmreclaimed);
		bcm_nvram_refresh(nvram_vars);
	}
#endif
	if (length)
		*length = vars_len;
	return nvram_vars;
}

/* copy nvram vars into locally-allocated multi-string array */
int BCMINITFN(bcm_nvram_cache) (void *sih)
{
	int ret = 0;
	void *osh;
	char *flash = NULL;

	if (vars_len >= 0) {
#ifndef BCMNVRAMR
		bcm_nvram_refresh(nvram_vars);
#endif
		return 0;
	}

	osh = si_osh((si_t *) sih);

	/* allocate memory and read in flash */
	flash = MALLOC(osh, NVRAM_SPACE);
	if (!flash) {
		ret = BCME_NOMEM;
		goto exit;
	}

	bcm_nvram_refresh(flash);
#ifdef BCMNVRAMR
	if (vars_len > 3) {
		/* copy into a properly-sized buffer */
		nvram_vars = MALLOC(osh, vars_len);
		if (!nvram_vars) {
			ret = BCME_NOMEM;
		} else
			bcopy(flash, nvram_vars, vars_len);
	}
	MFREE(osh, flash, NVRAM_SPACE);
#else
	/* cache must be full size of nvram if read/write */
	nvram_vars = flash;
#endif				/* BCMNVRAMR */

 exit:
	return ret;
}
#endif				/* WLC_LOW */

/* iovar table lookup */
const bcm_iovar_t *bcm_iovar_lookup(const bcm_iovar_t *table, const char *name)
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

	return NULL;		/* var name not found */
}

int bcm_iovar_lencheck(const bcm_iovar_t *vi, void *arg, int len, bool set)
{
	int bcmerror = 0;

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
		if (len < (int)sizeof(int)) {
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
		} else if (len) {
			/* Set is an action w/o parameters */
			bcmerror = BCME_BUFTOOLONG;
		}
		break;

	default:
		/* unknown type for length check in iovar info */
		ASSERT(0);
		bcmerror = BCME_UNSUPPORTED;
	}

	return bcmerror;
}

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

static const u8 crc8_table[256] = {
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
	((c) = ((c) >> 8) ^ crc##n##_table[((c) ^ (x)) & 0xff])

u8 BCMROMFN(hndcrc8) (u8 *pdata,	/* pointer to array of data to process */
			 uint nbytes,	/* number of input data bytes to process */
			 u8 crc	/* either CRC8_INIT_VALUE or previous return value */
    ) {
	/* hard code the crc loop instead of using CRC_INNER_LOOP macro
	 * to avoid the undefined and unnecessary (u8 >> 8) operation.
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

uint16 BCMROMFN(hndcrc16) (u8 *pdata,	/* pointer to array of data to process */
			   uint nbytes,	/* number of input data bytes to process */
			   uint16 crc	/* either CRC16_INIT_VALUE or previous return value */
    ) {
	while (nbytes-- > 0)
		CRC_INNER_LOOP(16, crc, *pdata++);
	return crc;
}

/*
 * Advance from the current 1-byte tag/1-byte length/variable-length value
 * triple, to the next, returning a pointer to the next.
 * If the current or next TLV is invalid (does not fit in given buffer length),
 * NULL is returned.
 * *buflen is not modified if the TLV elt parameter is invalid, or is decremented
 * by the TLV parameter's length if it is valid.
 */
bcm_tlv_t *BCMROMFN(bcm_next_tlv) (bcm_tlv_t *elt, int *buflen)
{
	int len;

	/* validate current elt */
	if (!bcm_valid_tlv(elt, *buflen))
		return NULL;

	/* advance to next elt */
	len = elt->len;
	elt = (bcm_tlv_t *) (elt->data + len);
	*buflen -= (2 + len);

	/* validate next elt */
	if (!bcm_valid_tlv(elt, *buflen))
		return NULL;

	return elt;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
bcm_tlv_t *BCMROMFN(bcm_parse_tlvs) (void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t *) buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= 2) {
		int len = elt->len;

		/* validate remaining totlen */
		if ((elt->id == key) && (totlen >= (len + 2)))
			return elt;

		elt = (bcm_tlv_t *) ((u8 *) elt + (len + 2));
		totlen -= (len + 2);
	}

	return NULL;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag.  Stop parsing when we see an element whose ID is greater
 * than the target key.
 */
bcm_tlv_t *BCMROMFN(bcm_parse_ordered_tlvs) (void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t *) buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= 2) {
		uint id = elt->id;
		int len = elt->len;

		/* Punt if we start seeing IDs > than target key */
		if (id > key)
			return NULL;

		/* validate remaining totlen */
		if ((id == key) && (totlen >= (len + 2)))
			return elt;

		elt = (bcm_tlv_t *) ((u8 *) elt + (len + 2));
		totlen -= (len + 2);
	}
	return NULL;
}

#if defined(BCMDBG)
int
bcm_format_flags(const bcm_bit_desc_t *bd, uint32 flags, char *buf, int len)
{
	int i;
	char *p = buf;
	char hexstr[16];
	int slen = 0, nlen = 0;
	uint32 bit;
	const char *name;

	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0; flags != 0; i++) {
		bit = bd[i].bit;
		name = bd[i].name;
		if (bit == 0 && flags != 0) {
			/* print any unnamed bits */
			snprintf(hexstr, 16, "0x%X", flags);
			name = hexstr;
			flags = 0;	/* exit loop */
		} else if ((flags & bit) == 0)
			continue;
		flags &= ~bit;
		nlen = strlen(name);
		slen += nlen;
		/* count btwn flag space */
		if (flags != 0)
			slen += 1;
		/* need NULL char as well */
		if (len <= slen)
			break;
		/* copy NULL char but don't count it */
		strncpy(p, name, nlen + 1);
		p += nlen;
		/* copy btwn flag space and NULL char */
		if (flags != 0)
			p += snprintf(p, 2, " ");
		len -= slen;
	}

	/* indicate the str was too short */
	if (flags != 0) {
		if (len < 2)
			p -= 2 - len;	/* overwrite last char */
		p += snprintf(p, 2, ">");
	}

	return (int)(p - buf);
}

/* print bytes formatted as hex to a string. return the resulting string length */
int bcm_format_hex(char *str, const void *bytes, int len)
{
	int i;
	char *p = str;
	const u8 *src = (const u8 *)bytes;

	for (i = 0; i < len; i++) {
		p += snprintf(p, 3, "%02X", *src);
		src++;
	}
	return (int)(p - str);
}
#endif				/* defined(BCMDBG) */

/* pretty hex print a contiguous buffer */
void prhex(const char *msg, uchar *buf, uint nbytes)
{
	char line[128], *p;
	int len = sizeof(line);
	int nchar;
	uint i;

	if (msg && (msg[0] != '\0'))
		printf("%s:\n", msg);

	p = line;
	for (i = 0; i < nbytes; i++) {
		if (i % 16 == 0) {
			nchar = snprintf(p, len, "  %04d: ", i);	/* line prefix */
			p += nchar;
			len -= nchar;
		}
		if (len > 0) {
			nchar = snprintf(p, len, "%02x ", buf[i]);
			p += nchar;
			len -= nchar;
		}

		if (i % 16 == 15) {
			printf("%s\n", line);	/* flush line */
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
	"NALG" "UNDEF",
	"UNDEF",
	"UNDEF",
	"UNDEF"
};

const char *bcm_crypto_algo_name(uint algo)
{
	return (algo <
		ARRAYSIZE(crypto_algo_names)) ? crypto_algo_names[algo] : "ERR";
}

#ifdef BCMDBG
void deadbeef(void *p, uint len)
{
	static u8 meat[] = { 0xde, 0xad, 0xbe, 0xef };

	while (len-- > 0) {
		*(u8 *) p = meat[((uintptr) p) & 3];
		p = (u8 *) p + 1;
	}
}
#endif				/* BCMDBG */

char *bcm_chipname(uint chipid, char *buf, uint len)
{
	const char *fmt;

	fmt = ((chipid > 0xa000) || (chipid < 0x4000)) ? "%d" : "%x";
	snprintf(buf, len, fmt, chipid);
	return buf;
}

/* Produce a human-readable string for boardrev */
char *bcm_brev_str(uint32 brev, char *buf)
{
	if (brev < 0x100)
		snprintf(buf, 8, "%d.%d", (brev & 0xf0) >> 4, brev & 0xf);
	else
		snprintf(buf, 8, "%c%03x",
			 ((brev & 0xf000) == 0x1000) ? 'P' : 'A', brev & 0xfff);

	return buf;
}

#define BUFSIZE_TODUMP_ATONCE 512	/* Buffer size */

/* dump large strings to console */
void printbig(char *buf)
{
	uint len, max_len;
	char c;

	len = strlen(buf);

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
bcmdumpfields(bcmutl_rdreg_rtn read_rtn, void *arg0, uint arg1,
	      struct fielddesc *fielddesc_array, char *buf, uint32 bufsize)
{
	uint filled_len;
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
		if (len < 0 || (uint32) len >= bufsize)
			len = bufsize - 1;
		buf += len;
		bufsize -= len;
		filled_len += len;
		cur_ptr++;
	}
	return filled_len;
}

uint bcm_mkiovar(char *name, char *data, uint datalen, char *buf, uint buflen)
{
	uint len;

	len = strlen(name) + 1;

	if ((len + datalen) > buflen)
		return 0;

	strncpy(buf, name, buflen);

	/* append data onto the end of the name string */
	memcpy(&buf[len], data, datalen);
	len += datalen;

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
#define QDBM_TABLE_LOW_BOUND 6493	/* Low bound */

/* Largest mW value that will round down to the last table entry,
 * QDBM_OFFSET + QDBM_TABLE_LEN-1.
 * Value is ( mW(QDBM_OFFSET + QDBM_TABLE_LEN - 1) + mW(QDBM_OFFSET + QDBM_TABLE_LEN) ) / 2.
 */
#define QDBM_TABLE_HIGH_BOUND 64938	/* High bound */

static const uint16 nqdBm_to_mW_map[QDBM_TABLE_LEN] = {
/* qdBm: 	+0 	+1 	+2 	+3 	+4 	+5 	+6 	+7 */
/* 153: */ 6683, 7079, 7499, 7943, 8414, 8913, 9441, 10000,
/* 161: */ 10593, 11220, 11885, 12589, 13335, 14125, 14962, 15849,
/* 169: */ 16788, 17783, 18836, 19953, 21135, 22387, 23714, 25119,
/* 177: */ 26607, 28184, 29854, 31623, 33497, 35481, 37584, 39811,
/* 185: */ 42170, 44668, 47315, 50119, 53088, 56234, 59566, 63096
};

uint16 BCMROMFN(bcm_qdbm_to_mw) (u8 qdbm)
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
	return (nqdBm_to_mW_map[idx] + factor / 2) / factor;
}

u8 BCMROMFN(bcm_mw_to_qdbm) (uint16 mw)
{
	u8 qdbm;
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

	for (qdbm = 0; qdbm < QDBM_TABLE_LEN - 1; qdbm++) {
		boundary = nqdBm_to_mW_map[qdbm] + (nqdBm_to_mW_map[qdbm + 1] -
						    nqdBm_to_mW_map[qdbm]) / 2;
		if (mw_uint < boundary)
			break;
	}

	qdbm += (u8) offset;

	return qdbm;
}

uint BCMROMFN(bcm_bitcount) (u8 *bitmap, uint length)
{
	uint bitcount = 0, i;
	u8 tmp;
	for (i = 0; i < length; i++) {
		tmp = bitmap[i];
		while (tmp) {
			bitcount++;
			tmp &= (tmp - 1);
		}
	}
	return bitcount;
}

/* Initialization of bcmstrbuf structure */
void bcm_binit(struct bcmstrbuf *b, char *buf, uint size)
{
	b->origsize = b->size = size;
	b->origbuf = b->buf = buf;
}

/* Buffer sprintf wrapper to guard against buffer overflow */
int bcm_bprintf(struct bcmstrbuf *b, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(b->buf, b->size, fmt, ap);

	/* Non Ansi C99 compliant returns -1,
	 * Ansi compliant return r >= b->size,
	 * bcmstdlib returns 0, handle all
	 */
	if ((r == -1) || (r >= (int)b->size) || (r == 0)) {
		b->size = 0;
	} else {
		b->size -= r;
		b->buf += r;
	}

	va_end(ap);

	return r;
}

void bcm_inc_bytes(uchar *num, int num_bytes, u8 amount)
{
	int i;

	for (i = 0; i < num_bytes; i++) {
		num[i] += amount;
		if (num[i] >= amount)
			break;
		amount = 1;
	}
}

int bcm_cmp_bytes(uchar *arg1, uchar *arg2, u8 nbytes)
{
	int i;

	for (i = nbytes - 1; i >= 0; i--) {
		if (arg1[i] != arg2[i])
			return arg1[i] - arg2[i];
	}
	return 0;
}

void bcm_print_bytes(char *name, const uchar *data, int len)
{
	int i;
	int per_line = 0;

	printf("%s: %d\n", name ? name : "", len);
	for (i = 0; i < len; i++) {
		printf("%02x ", *data++);
		per_line++;
		if (per_line == 16) {
			per_line = 0;
			printf("\n");
		}
	}
	printf("\n");
}

#if defined(BCMDBG)
#define SSID_FMT_BUF_LEN	((4 * DOT11_MAX_SSID_LEN) + 1)
int bcm_format_ssid(char *buf, const uchar ssid[], uint ssid_len)
{
	uint i, c;
	char *p = buf;
	char *endp = buf + SSID_FMT_BUF_LEN;

	if (ssid_len > DOT11_MAX_SSID_LEN)
		ssid_len = DOT11_MAX_SSID_LEN;

	for (i = 0; i < ssid_len; i++) {
		c = (uint) ssid[i];
		if (c == '\\') {
			*p++ = '\\';
			*p++ = '\\';
		} else if (isprint((uchar) c)) {
			*p++ = (char)c;
		} else {
			p += snprintf(p, (endp - p), "\\x%02X", c);
		}
	}
	*p = '\0';
	ASSERT(p < endp);

	return (int)(p - buf);
}
#endif				/* defined(BCMDBG) */
