/*
 * Driver O/S-independent utility routines
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
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
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmutils.c 699163 2017-05-12 05:18:23Z $
 */

#include <bcm_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <stdarg.h>
#ifdef BCMDRIVER

#include <osl.h>
#include <bcmutils.h>

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

/* Look-up table to calculate head room present in a number */
static const uint8 msb_table[] = {
	0, 1, 2, 2, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8,
};

void *_bcmutils_dummy_fn = NULL;




#ifdef BCMDRIVER


/* copy a pkt buffer chain into a buffer */
uint
pktcopy(osl_t *osh, void *p, uint offset, int len, uchar *buf)
{
	uint n, ret = 0;

	if (len < 0)
		len = 4096;	/* "infinite" */

	/* skip 'offset' bytes */
	for (; p && offset; p = PKTNEXT(osh, p)) {
		if (offset < (uint)PKTLEN(osh, p))
			break;
		offset -= PKTLEN(osh, p);
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = PKTNEXT(osh, p)) {
		n = MIN((uint)PKTLEN(osh, p) - offset, (uint)len);
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
pktfrombuf(osl_t *osh, void *p, uint offset, int len, uchar *buf)
{
	uint n, ret = 0;


	/* skip 'offset' bytes */
	for (; p && offset; p = PKTNEXT(osh, p)) {
		if (offset < (uint)PKTLEN(osh, p))
			break;
		offset -= PKTLEN(osh, p);
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = PKTNEXT(osh, p)) {
		n = MIN((uint)PKTLEN(osh, p) - offset, (uint)len);
		bcopy(buf, PKTDATA(osh, p) + offset, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}



/* return total length of buffer chain */
uint BCMFASTPATH
pkttotlen(osl_t *osh, void *p)
{
	uint total;
	int len;

	total = 0;
	for (; p; p = PKTNEXT(osh, p)) {
		len = PKTLEN(osh, p);
		total += len;
#ifdef BCMLFRAG
		if (BCMLFRAG_ENAB()) {
			if (PKTISFRAG(osh, p)) {
				total += PKTFRAGTOTLEN(osh, p);
			}
		}
#endif
	}

	return (total);
}

/* return the last buffer of chained pkt */
void *
pktlast(osl_t *osh, void *p)
{
	for (; PKTNEXT(osh, p); p = PKTNEXT(osh, p))
		;

	return (p);
}

/* count segments of a chained packet */
uint BCMFASTPATH
pktsegcnt(osl_t *osh, void *p)
{
	uint cnt;

	for (cnt = 0; p; p = PKTNEXT(osh, p)) {
		cnt++;
#ifdef BCMLFRAG
		if (BCMLFRAG_ENAB()) {
			if (PKTISFRAG(osh, p)) {
				cnt += PKTFRAGTOTNUM(osh, p);
			}
		}
#endif
	}

	return cnt;
}


/* count segments of a chained packet */
uint BCMFASTPATH
pktsegcnt_war(osl_t *osh, void *p)
{
	uint cnt;
	uint8 *pktdata;
	uint len, remain, align64;

	for (cnt = 0; p; p = PKTNEXT(osh, p)) {
		cnt++;
		len = PKTLEN(osh, p);
		if (len > 128) {
			pktdata = (uint8 *)PKTDATA(osh, p);	/* starting address of data */
			/* Check for page boundary straddle (2048B) */
			if (((uintptr)pktdata & ~0x7ff) != ((uintptr)(pktdata+len) & ~0x7ff))
				cnt++;

			align64 = (uint)((uintptr)pktdata & 0x3f);	/* aligned to 64B */
			align64 = (64 - align64) & 0x3f;
			len -= align64;		/* bytes from aligned 64B to end */
			/* if aligned to 128B, check for MOD 128 between 1 to 4B */
			remain = len % 128;
			if (remain > 0 && remain <= 4)
				cnt++;		/* add extra seg */
		}
	}

	return cnt;
}

uint8 * BCMFASTPATH
pktdataoffset(osl_t *osh, void *p,  uint offset)
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

#endif /* BCMDRIVER */

#if !defined(BCMROMOFFLOAD_EXCLUDE_BCMUTILS_FUNCS)
const unsigned char bcm_ctype[] = {

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

ulong
bcm_strtoul(const char *cp, char **endp, uint base)
{
	ulong result, last_result = 0, value;
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

int
bcm_atoi(const char *s)
{
	return (int)bcm_strtoul(s, NULL, 10);
}

/* return pointer to location of substring 'needle' in 'haystack' */
char *
bcmstrstr(const char *haystack, const char *needle)
{
	int len, nlen;
	int i;

	if ((haystack == NULL) || (needle == NULL))
		return DISCARD_QUAL(haystack, char);

	nlen = (int)strlen(needle);
	len = (int)strlen(haystack) - nlen + 1;

	for (i = 0; i < len; i++)
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


#if defined(CONFIG_USBRNDIS_RETAIL) || defined(NDIS_MINIPORT_DRIVER)
/* registry routine buffer preparation utility functions:
 * parameter order is like strncpy, but returns count
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

char *
bcm_ip_ntoa(struct ipv4_addr *ia, char *buf)
{
	snprintf(buf, 16, "%d.%d.%d.%d",
	         ia->addr[0], ia->addr[1], ia->addr[2], ia->addr[3]);
	return (buf);
}

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
			snprintf(p, 16, ":%u.%u.%u.%u", a4[0], a4[1], a4[2], a4[3]);
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
#ifdef BCMDRIVER

void
bcm_mdelay(uint ms)
{
	uint i;

	for (i = 0; i < ms; i++) {
		OSL_DELAY(1000);
	}
}





#if defined(DHD_DEBUG)
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
#endif	

/* Takes an Ethernet frame and sets out-of-bound PKTPRIO.
 * Also updates the inplace vlan tag if requested.
 * For debugging, it returns an indication of what it did.
 */
uint BCMFASTPATH
pktsetprio(void *pkt, bool update_vtag)
{
	struct ether_header *eh;
	struct ethervlan_header *evh;
	uint8 *pktdata;
	int priority = 0;
	int rc = 0;

	pktdata = (uint8 *)PKTDATA(OSH_NULL, pkt);
	ASSERT(ISALIGNED((uintptr)pktdata, sizeof(uint16)));

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
	} else if ((eh->ether_type == hton16(ETHER_TYPE_IP)) ||
		(eh->ether_type == hton16(ETHER_TYPE_IPV6))) {
		uint8 *ip_body = pktdata + sizeof(struct ether_header);
		uint8 tos_tc = IP_TOS46(ip_body);
		uint8 dscp = tos_tc >> IPV4_TOS_DSCP_SHIFT;
		switch (dscp) {
		case DSCP_EF:
			priority = PRIO_8021D_VO;
			break;
		case DSCP_AF31:
		case DSCP_AF32:
		case DSCP_AF33:
			priority = PRIO_8021D_CL;
			break;
		case DSCP_AF21:
		case DSCP_AF22:
		case DSCP_AF23:
		case DSCP_AF11:
		case DSCP_AF12:
		case DSCP_AF13:
			priority = PRIO_8021D_EE;
			break;
		default:
			priority = (int)(tos_tc >> IPV4_TOS_PREC_SHIFT);
			break;
		}

		rc |= PKTPRIO_DSCP;
	}

	ASSERT(priority >= 0 && priority <= MAXPRIO);
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
uint BCMFASTPATH
pktsetprio_qms(void *pkt, uint8* up_table, bool update_vtag)
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
bool BCMFASTPATH
pktgetdscp(uint8 *pktdata, uint pktlen, uint8 *dscp)
{
	struct ether_header *eh;
	struct ethervlan_header *evh;
	uint8 *ip_body;
	bool rc = FALSE;

	/* minimum length is ether header and IP header */
	if (pktlen < sizeof(struct ether_header) + IPV4_MIN_HEADER_LEN)
		return FALSE;

	eh = (struct ether_header *) pktdata;

	if (eh->ether_type == HTON16(ETHER_TYPE_IP)) {
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
int BCMFASTPATH
wl_set_up_table(uint8 *up_table, bcm_tlv_t *qos_map_ie)
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

/* The 0.5KB string table is not removed by compiler even though it's unused */

static char bcm_undeferrstr[32];
static const char *bcmerrorstrtable[] = BCMERRSTRINGTABLE;

/* Convert the error codes into related error strings  */
const char *
bcmerrorstr(int bcmerror)
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
bcm_iovar_lencheck(const bcm_iovar_t *vi, void *arg, int len, bool set)
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

#endif	/* BCMDRIVER */

#ifdef BCM_OBJECT_TRACE

#define BCM_OBJECT_MERGE_SAME_OBJ	0

/* some place may add / remove the object to trace list for Linux: */
/* add:    osl_alloc_skb dev_alloc_skb skb_realloc_headroom dhd_start_xmit */
/* remove: osl_pktfree dev_kfree_skb netif_rx */

#define BCM_OBJDBG_COUNT          (1024 * 100)
static spinlock_t dbgobj_lock;
#define	BCM_OBJDBG_LOCK_INIT()    spin_lock_init(&dbgobj_lock)
#define	BCM_OBJDBG_LOCK_DESTROY()
#define	BCM_OBJDBG_LOCK           spin_lock_irqsave
#define	BCM_OBJDBG_UNLOCK         spin_unlock_irqrestore

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
		printf("%s: not all objects are released\n", __FUNCTION__);
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
				printf("%s: obj %p allocated from %s(%d),"
					" allocate again from %s(%d)\n",
					__FUNCTION__, dbgobj->obj,
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
			printf("%s: already got %d objects ?????????????????????\n",
				__FUNCTION__, BCM_OBJDBG_COUNT);
			ASSERT(0);
			goto EXIT;
		}

		bcm_object_rm_list(&dbgobj_freehead, &dbgobj_freetail, dbgobj);
		dbgobj->obj = obj;
		strncpy(dbgobj->caller, caller, BCM_OBJDBG_CALLER_LEN);
		dbgobj->caller[BCM_OBJDBG_CALLER_LEN-1] = '\0';
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
					printf("%s: rm flagged obj %p flag 0x%08x from %s(%d)\n",
						__FUNCTION__, obj, dbgobj->flag, caller, line);
				}
				bcm_object_rm_list(&dbgobj_objhead, &dbgobj_objtail, dbgobj);
				memset(dbgobj->caller, 0x00, BCM_OBJDBG_CALLER_LEN);
				strncpy(dbgobj->caller, caller, BCM_OBJDBG_CALLER_LEN);
				dbgobj->caller[BCM_OBJDBG_CALLER_LEN-1] = '\0';
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
				printf("%s: obj %p already freed from from %s(%d),"
					" try free again from %s(%d)\n",
					__FUNCTION__, obj,
					dbgobj->caller, dbgobj->line,
					caller, line);
				//ASSERT(0); /* release same obj more than one time? */
				goto EXIT;
			}
			dbgobj = dbgobj->prior;
			if (dbgobj == dbgobj_freetail)
				break;
		}

		printf("%s: ################### release none-existing obj %p from %s(%d)\n",
			__FUNCTION__, obj, caller, line);
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
			printf("%s: (%s:%d) obj %p (sn %d state %d) was freed from %s(%d)\n",
				__FUNCTION__, caller, line,
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

	printf("%s: obj %p not found, check from %s(%d), chksn %s, sn %d\n",
		__FUNCTION__, obj, caller, line, chksn ? "yes" : "no", sn);
	dbgobj = dbgobj_objtail;
	while (dbgobj) {
		printf("%s: (%s:%d) obj %p sn %d was allocated from %s(%d)\n",
				__FUNCTION__, caller, line,
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

	printf("%s: obj %p not found in active list\n", __FUNCTION__, obj);
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

	printf("%s: obj %p not found in active list\n", __FUNCTION__, obj);
	ASSERT(0);

EXIT:
	BCM_OBJDBG_UNLOCK(&dbgobj_lock, flags);
	return rtn;
}

#endif /* BCM_OBJECT_TRACE */

uint8 *
bcm_write_tlv(int type, const void *data, int datalen, uint8 *dst)
{
	uint8 *new_dst = dst;
	bcm_tlv_t *dst_tlv = (bcm_tlv_t *)dst;

	/* dst buffer should always be valid */
	ASSERT(dst);

	/* data len must be within valid range */
	ASSERT((datalen >= 0) && (datalen <= BCM_TLV_MAX_DATA_SIZE));

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
	    ((datalen >= 0) && (datalen <= BCM_TLV_MAX_DATA_SIZE)) &&
	    ((data != NULL) || (datalen == 0))) {

	        /* write type, len fields */
		dst_tlv->id = (uint8)type;
	        dst_tlv->len = (uint8)datalen;

		/* if data is present, copy to the output buffer and update
		 * pointer to output buffer
		 */
		if (datalen > 0) {

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
bcm_write_tlv_safe(int type, const void *data, int datalen, uint8 *dst, int dst_maxlen)
{
	uint8 *new_dst = dst;

	if ((datalen >= 0) && (datalen <= BCM_TLV_MAX_DATA_SIZE)) {

		/* if len + tlv hdr len is more than destlen, don't do anything
		 * just return the buffer untouched
		 */
		if ((int)(datalen + BCM_TLV_HDR_SIZE) <= dst_maxlen) {

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


uint8 *bcm_copy_tlv_safe(const void *src, uint8 *dst, int dst_maxlen)
{
	uint8 *new_dst = dst;
	const bcm_tlv_t *src_tlv = (const bcm_tlv_t *)src;

	ASSERT(src);
	if (src) {
		if (bcm_valid_tlv(src_tlv, dst_maxlen)) {
			new_dst = bcm_copy_tlv(src, dst);
		}
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
	uint8 *pdata,	/* pointer to array of data to process */
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
    uint8 *pdata,  /* pointer to array of data to process */
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
hndcrc32(uint8 *pdata, uint nbytes, uint32 crc)
{
	uint8 *pend;
	pend = pdata + nbytes;
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);

	return crc;
}

#ifdef notdef
#define CLEN 	1499 	/*  CRC Length */
#define CBUFSIZ 	(CLEN+4)
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
#endif /* notdef */

/*
 * Advance from the current 1-byte tag/1-byte length/variable-length value
 * triple, to the next, returning a pointer to the next.
 * If the current or next TLV is invalid (does not fit in given buffer length),
 * NULL is returned.
 * *buflen is not modified if the TLV elt parameter is invalid, or is decremented
 * by the TLV parameter's length if it is valid.
 */
bcm_tlv_t *
bcm_next_tlv(bcm_tlv_t *elt, int *buflen)
{
	int len;

	/* validate current elt */
	if (!bcm_valid_tlv(elt, *buflen)) {
		return NULL;
	}

	/* advance to next elt */
	len = elt->len;
	elt = (bcm_tlv_t*)(elt->data + len);
	*buflen -= (TLV_HDR_LEN + len);

	/* validate next elt */
	if (!bcm_valid_tlv(elt, *buflen)) {
		return NULL;
	}

	return elt;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
bcm_tlv_t *
bcm_parse_tlvs(void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	if ((elt = (bcm_tlv_t*)buf) == NULL) {
		return NULL;
	}
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		int len = elt->len;

		/* validate remaining totlen */
		if ((elt->id == key) && (totlen >= (int)(len + TLV_HDR_LEN))) {

			return (elt);
		}

		elt = (bcm_tlv_t*)((uint8*)elt + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);
	}

	return NULL;
}

bcm_tlv_t *
bcm_parse_tlvs_dot11(void *buf, int buflen, uint key, bool id_ext)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t*)buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		int len = elt->len;

		do {
			/* validate remaining totlen */
			if (totlen <  (int)(len + TLV_HDR_LEN))
				break;

			if (id_ext) {
				if (!DOT11_MNG_IE_ID_EXT_MATCH(elt, key))
					break;
			} else if (elt->id != key) {
				break;
			}

			return (elt);
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
bcm_parse_tlvs_min_bodylen(void *buf, int buflen, uint key, int min_bodylen)
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
 * matches tag.  Stop parsing when we see an element whose ID is greater
 * than the target key.
 */
bcm_tlv_t *
bcm_parse_ordered_tlvs(void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t*)buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= TLV_HDR_LEN) {
		uint id = elt->id;
		int len = elt->len;

		/* Punt if we start seeing IDs > than target key */
		if (id > key) {
			return (NULL);
		}

		/* validate remaining totlen */
		if ((id == key) && (totlen >= (int)(len + TLV_HDR_LEN))) {
			return (elt);
		}

		elt = (bcm_tlv_t*)((uint8*)elt + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);
	}
	return NULL;
}
#endif	/* !BCMROMOFFLOAD_EXCLUDE_BCMUTILS_FUNCS */

#if defined(WLMSG_PRHDRS) || defined(WLMSG_PRPKT) || defined(WLMSG_ASSOC) || \
	defined(DHD_DEBUG)
int
bcm_format_field(const bcm_bit_desc_ex_t *bd, uint32 flags, char* buf, int len)
{
	int i, slen = 0;
	uint32 bit, mask;
	const char *name;
	mask = bd->mask;
	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0;  (name = bd->bitfield[i].name) != NULL; i++) {
		bit = bd->bitfield[i].bit;
		if ((flags & mask) == bit) {
			if (len > (int)strlen(name)) {
				slen = strlen(name);
				strncpy(buf, name, slen+1);
			}
			break;
		}
	}
	return slen;
}

int
bcm_format_flags(const bcm_bit_desc_t *bd, uint32 flags, char* buf, int len)
{
	int i;
	char* p = buf;
	char hexstr[16];
	int slen = 0, nlen = 0;
	uint32 bit;
	const char* name;

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
	}

	/* indicate the str was too short */
	if (flags != 0) {
		p += snprintf(p, 2, ">");
	}

	return (int)(p - buf);
}
#endif 

/* print bytes formatted as hex to a string. return the resulting string length */
int
bcm_format_hex(char *str, const void *bytes, int len)
{
	int i;
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
prhex(const char *msg, volatile uchar *buf, uint nbytes)
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
	"UNDEF"
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

#define BUFSIZE_TODUMP_ATONCE 512 /* Buffer size */

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

	strncpy(buf, name, buflen);

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
/* qdBm: 	+0 	+1 	+2 	+3 	+4 	+5 	+6 	+7 */
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
bcm_bitcount(uint8 *bitmap, uint length)
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

#if defined(BCMDRIVER) || defined(WL_UNITTEST)

/* triggers bcm_bprintf to print to kernel log */
bool bcm_bprintf_bypass = FALSE;

/* Initialization of bcmstrbuf structure */
void
bcm_binit(struct bcmstrbuf *b, char *buf, uint size)
{
	b->origsize = b->size = size;
	b->origbuf = b->buf = buf;
}

/* Buffer sprintf wrapper to guard against buffer overflow */
int
bcm_bprintf(struct bcmstrbuf *b, const char *fmt, ...)
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
bcm_bprhex(struct bcmstrbuf *b, const char *msg, bool newline, const uint8 *buf, int len)
{
	int i;

	if (msg != NULL && msg[0] != '\0')
		bcm_bprintf(b, "%s", msg);
	for (i = 0; i < len; i ++)
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
bcm_print_bytes(const char *name, const uchar *data, int len)
{
	int i;
	int per_line = 0;

	printf("%s: %d \n", name ? name : "", len);
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

/* Look for vendor-specific IE with specified OUI and optional type */
bcm_tlv_t *
bcm_find_vendor_ie(void *tlvs, int tlvs_len, const char *voui, uint8 *type, int type_len)
{
	bcm_tlv_t *ie;
	uint8 ie_len;

	ie = (bcm_tlv_t*)tlvs;

	/* make sure we are looking at a valid IE */
	if (ie == NULL || !bcm_valid_tlv(ie, tlvs_len)) {
		return NULL;
	}

	/* Walk through the IEs looking for an OUI match */
	do {
		ie_len = ie->len;
		if ((ie->id == DOT11_MNG_PROPR_ID) &&
		    (ie_len >= (DOT11_OUI_LEN + type_len)) &&
		    !bcmp(ie->data, voui, DOT11_OUI_LEN))
		{
			/* compare optional type */
			if (type_len == 0 ||
			    !bcmp(&ie->data[DOT11_OUI_LEN], type, type_len)) {
				return (ie);		/* a match */
			}
		}
	} while ((ie = bcm_next_tlv(ie, &tlvs_len)) != NULL);

	return NULL;
}

#if defined(WLTINYDUMP) || defined(WLMSG_INFORM) || defined(WLMSG_ASSOC) || \
	defined(WLMSG_PRPKT) || defined(WLMSG_WSEC)
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
#endif 

#endif /* BCMDRIVER || WL_UNITTEST */

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
	char nv_ver[128];

	dp = varbuf;

	findNewline = FALSE;
	column = 0;

	// terence 20130914: print out NVRAM version
	if (varbuf[0] == '#') {
		memset(nv_ver, 0x00, sizeof(nv_ver));
		for (n=1; n<len && n<(sizeof(nv_ver)-1); n++) {
			if (varbuf[n] == '\n')
				break;
			nv_ver[n-1] = varbuf[n];
		}
		printk("NVRAM version: %s\n", nv_ver);
	}

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

/* calculate a * b + c */
void
bcm_uint64_multiple_add(uint32* r_high, uint32* r_low, uint32 a, uint32 b, uint32 c)
{
#define FORMALIZE(var) {cc += (var & 0x80000000) ? 1 : 0; var &= 0x7fffffff;}
	uint32 r1, r0;
	uint32 a1, a0, b1, b0, t, cc = 0;

	a1 = a >> 16;
	a0 = a & 0xffff;
	b1 = b >> 16;
	b0 = b & 0xffff;

	r0 = a0 * b0;
	FORMALIZE(r0);

	t = (a1 * b0) << 16;
	FORMALIZE(t);

	r0 += t;
	FORMALIZE(r0);

	t = (a0 * b1) << 16;
	FORMALIZE(t);

	r0 += t;
	FORMALIZE(r0);

	FORMALIZE(c);

	r0 += c;
	FORMALIZE(r0);

	r0 |= (cc % 2) ? 0x80000000 : 0;
	r1 = a1 * b1 + ((a1 * b0) >> 16) + ((b1 * a0) >> 16) + (cc / 2);

	*r_high = r1;
	*r_low = r0;
}

/* calculate a / b */
void
bcm_uint64_divide(uint32* r, uint32 a_high, uint32 a_low, uint32 b)
{
	uint32 a1 = a_high, a0 = a_low, r0 = 0;

	if (b < 2)
		return;

	while (a1 != 0) {
		r0 += (0xffffffff / b) * a1;
		bcm_uint64_multiple_add(&a1, &a0, ((0xffffffff % b) + 1) % b, a1, a0);
	}

	r0 += a0 / b;
	*r = r0;
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
set_bitrange(void *array, uint start, uint end, uint maxbit)
{
	uint startbyte = start/NBBY;
	uint endbyte = end/NBBY;
	uint i, startbytelastbit, endbytestartbit;

	if (end >= start) {
		if (endbyte - startbyte > 1)
		{
			startbytelastbit = (startbyte+1)*NBBY - 1;
			endbytestartbit = endbyte*NBBY;
			for (i = startbyte+1; i < endbyte; i++)
				((uint8 *)array)[i] = 0xFF;
			for (i = start; i <= startbytelastbit; i++)
				setbit(array, i);
			for (i = endbytestartbit; i <= end; i++)
				setbit(array, i);
		} else {
			for (i = start; i <= end; i++)
				setbit(array, i);
		}
	}
	else {
		set_bitrange(array, start, maxbit, maxbit);
		set_bitrange(array, 0, end, maxbit);
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
#if defined(BCMDRIVER) && !defined(_CFEZ_)
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
#define BCM_MWBMAP_ITEMS_MAX    (64 * 1024)  /* May increase to 64K */

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
bcm_mwbmap_init(osl_t *osh, uint32 items_max)
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
bcm_mwbmap_fini(osl_t * osh, struct bcm_mwbmap * mwbmap_hdl)
{
	bcm_mwbmap_t * mwbmap_p;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	MFREE(osh, mwbmap_p, sizeof(struct bcm_mwbmap)
	                     + (sizeof(uint32) * mwbmap_p->imaps));
	return;
}

/* Allocate a unique small index using a multiword bitmap index allocator.    */
uint32 BCMFASTPATH
bcm_mwbmap_alloc(struct bcm_mwbmap * mwbmap_hdl)
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
			bitmap   = (uint32)(((int)(bitmap)) & (-((int)(bitmap))));
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
			bitmap   = (uint32)(((int)(bitmap)) & (-((int)(bitmap))));
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
void BCMFASTPATH
bcm_mwbmap_free(struct bcm_mwbmap * mwbmap_hdl, uint32 bitix)
{
	bcm_mwbmap_t * mwbmap_p;
	uint32 wordix, bitmap, *bitmap_p;

	BCM_MWBMAP_AUDIT(mwbmap_hdl);
	mwbmap_p = BCM_MWBMAP_PTR(mwbmap_hdl);

	ASSERT(bitix < mwbmap_p->total);

	/* Start with second level hierarchy */
	wordix   = BCM_MWBMAP_DIVOP(bitix);
	bitmap   = (1U << BCM_MWBMAP_MODOP(bitix));
	bitmap_p = &mwbmap_p->id_bitmap[wordix];

	ASSERT((*bitmap_p & bitmap) == 0U);	/* ASSERT not a double free */

	mwbmap_p->ifree++; /* update free count */
	ASSERT(mwbmap_p->ifree <= mwbmap_p->total);

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

	printf("mwbmap_p %p wmaps %u imaps %u ifree %d total %u\n", mwbmap_p,
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
		id16_map->dbg = NULL;
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

uint16 BCMFASTPATH /* Allocate a unique 16bit id */
id16_map_alloc(void * id16_map_hndl)
{
	uint16 val16;
	id16_map_t * id16_map;

	ASSERT(id16_map_hndl != NULL);

	id16_map = (id16_map_t *)id16_map_hndl;

	ASSERT(id16_map->total > 0);

	if (id16_map->stack_idx < 0) {
		id16_map->failures++;
		return ID16_INVALID;
	}

	val16 = id16_map->stack[id16_map->stack_idx];
	id16_map->stack_idx--;

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	ASSERT((id16_map->start == ID16_UNDEFINED) ||
	       (val16 < (id16_map->start + id16_map->total)));

	if (id16_map->dbg) { /* Validate val16 */
		id16_map_dbg_t *id16_map_dbg = (id16_map_dbg_t *)id16_map->dbg;

		ASSERT(id16_map_dbg->avail[val16 - id16_map->start] == TRUE);
		id16_map_dbg->avail[val16 - id16_map->start] = FALSE;
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */

	return val16;
}


void BCMFASTPATH /* Free a 16bit id value into the id16 allocator */
id16_map_free(void * id16_map_hndl, uint16 val16)
{
	id16_map_t * id16_map;

	ASSERT(id16_map_hndl != NULL);

	id16_map = (id16_map_t *)id16_map_hndl;

#if defined(BCM_DBG) && defined(BCM_DBG_ID16)
	ASSERT((id16_map->start == ID16_UNDEFINED) ||
	       (val16 < (id16_map->start + id16_map->total)));

	if (id16_map->dbg) { /* Validate val16 */
		id16_map_dbg_t *id16_map_dbg = (id16_map_dbg_t *)id16_map->dbg;

		ASSERT(id16_map_dbg->avail[val16 - id16_map->start] == FALSE);
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
					id16_map_hndl, idx, val16));
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
				id16_map_hndl, avail, id16_map->stack_idx));
		}
	}
#endif /* BCM_DBG && BCM_DBG_ID16 */

done:
	/* invoke any other system audits */
	return (!!insane);
}
/* END: Simple id16 allocator */


#endif 

/* calculate a >> b; and returns only lower 32 bits */
void
bcm_uint64_right_shift(uint32* r, uint32 a_high, uint32 a_low, uint32 b)
{
	uint32 a1 = a_high, a0 = a_low, r0 = 0;

	if (b == 0) {
		r0 = a_low;
		*r = r0;
		return;
	}

	if (b < 32) {
		a0 = a0 >> b;
		a1 = a1 & ((1 << b) - 1);
		a1 = a1 << (32 - b);
		r0 = a0 | a1;
		*r = r0;
		return;
	} else {
		r0 = a1 >> (b - 32);
		*r = r0;
		return;
	}

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

/* Does unsigned 64 bit fixed point multiplication */
uint64
fp_mult_64(uint64 val1, uint64 val2, uint8 nf1, uint8 nf2, uint8 nf_res)
{
	uint64 mult_out_tmp, mult_out, rnd_val;
	uint8 shift_amt;

	shift_amt = nf1 + nf2 - nf_res;
	/* 0.5 in 1.0.shift_amt */
	rnd_val = bcm_shl_64(1, (shift_amt - 1));
	rnd_val = (shift_amt == 0) ? 0 : rnd_val;
	mult_out_tmp = (uint64)((uint64)val1 * (uint64)val2) + (uint64)rnd_val;
	mult_out = bcm_shr_64(mult_out_tmp, shift_amt);

	return mult_out;
}


/* Does unsigned 64 bit by 32 bit fixed point division */
uint8
fp_div_64(uint64 num, uint32 den, uint8 nf_num, uint8 nf_den, uint32 *div_out)
{
	uint8 shift_amt1, shift_amt2, shift_amt, nf_res, hd_rm_nr, hd_rm_dr;
	uint32 num_hi, num_lo;
	uint64 num_scale;

	/* Worst case shift possible */
	hd_rm_nr = fp_calc_head_room_64(num);
	hd_rm_dr = fp_calc_head_room_32(den);

	/* (Nr / Dr) <= 2^32 */
	shift_amt1 = hd_rm_nr - hd_rm_dr - 1;
	/* Shift <= 32 + N2 - N1 */
	shift_amt2 = 31 + nf_den - nf_num;
	shift_amt = MINIMUM(shift_amt1, shift_amt2);

	/* Scale numerator */
	num_scale = bcm_shl_64(num, shift_amt);

	/* Do division */
	num_hi = (uint32)((uint64)num_scale >> 32) & MASK_32_BITS;
	num_lo = (uint32)(num_scale & MASK_32_BITS);
	bcm_uint64_divide(div_out, num_hi, num_lo, den);

	/* Result format */
	nf_res = nf_num - nf_den + shift_amt;
	return nf_res;
}

/* Finds the number of bits available for shifting in unsigned 64 bit number */
uint8
fp_calc_head_room_64(uint64 num)
{
	uint8 n_room_bits = 0, msb_pos;
	uint32 num_hi, num_lo, x;

	num_hi = (uint32)((uint64)num >> 32) & MASK_32_BITS;
	num_lo = (uint32)(num & MASK_32_BITS);

	if (num_hi > 0) {
		x = num_hi;
		n_room_bits = 0;
	} else {
		x = num_lo;
		n_room_bits = 32;
	}

	msb_pos = (x >> 16) ? ((x >> 24) ? (24 + msb_table[(x >> 24) & MASK_8_BITS])
			: (16 + msb_table[(x >> 16) & MASK_8_BITS]))
			: ((x >> 8) ? (8 + msb_table[(x >> 8) & MASK_8_BITS])
			: msb_table[x & MASK_8_BITS]);

	return (n_room_bits + 32 - msb_pos);
}

/* Finds the number of bits available for shifting in unsigned 32 bit number */
uint8
fp_calc_head_room_32(uint32 x)
{
	uint8 msb_pos;

	msb_pos = (x >> 16) ? ((x >> 24) ? (24 + msb_table[(x >> 24) & MASK_8_BITS])
			: (16 + msb_table[(x >> 16) & MASK_8_BITS]))
			: ((x >> 8) ? (8 + msb_table[(x >> 8) & MASK_8_BITS])
			: msb_table[x & MASK_8_BITS]);

	return (32 - msb_pos);
}

/* Does unsigned 64 bit fixed point floor */
uint32
fp_floor_64(uint64 num, uint8 floor_pos)
{
	uint32 floor_out;

	floor_out = (uint32)bcm_shr_64(num, floor_pos);

	return floor_out;
}

/* Does unsigned 32 bit fixed point floor */
uint32
fp_floor_32(uint32 num, uint8 floor_pos)
{
	return num >> floor_pos;
}

/* Does unsigned 64 bit fixed point rounding */
uint32
fp_round_64(uint64 num, uint8 rnd_pos)
{
	uint64 rnd_val, rnd_out_tmp;
	uint32 rnd_out;

	/* 0.5 in 1.0.rnd_pos */
	rnd_val = bcm_shl_64(1, (rnd_pos - 1));
	rnd_val = (rnd_pos == 0) ? 0 : rnd_val;
	rnd_out_tmp = num + rnd_val;
	rnd_out = (uint32)bcm_shr_64(rnd_out_tmp, rnd_pos);

	return rnd_out;
}

/* Does unsigned 32 bit fixed point rounding */
uint32
fp_round_32(uint32 num, uint8 rnd_pos)
{
	uint32 rnd_val, rnd_out_tmp;

	/* 0.5 in 1.0.rnd_pos */
	rnd_val = 1 << (rnd_pos - 1);
	rnd_val = (rnd_pos == 0) ? 0 : rnd_val;
	rnd_out_tmp = num + rnd_val;
	return (rnd_out_tmp >> rnd_pos);
}

/* Does unsigned fixed point ceiling */
uint32
fp_ceil_64(uint64 num, uint8 ceil_pos)
{
	uint64 ceil_val, ceil_out_tmp;
	uint32 ceil_out;

	/* 0.999 in 1.0.rnd_pos */
	ceil_val = bcm_shl_64(1, ceil_pos) - 1;
	ceil_out_tmp = num + ceil_val;
	ceil_out = (uint32)bcm_shr_64(ceil_out_tmp, ceil_pos);

	return ceil_out;
}

/* Does left shift of unsigned 64 bit number */
uint64
bcm_shl_64(uint64 input, uint8 shift_amt)
{
	uint32 in_hi, in_lo;
	uint32 masked_lo = 0;
	uint32 mask;
	uint64 shl_out;

	if (shift_amt == 0) {
		return input;
	}

	/* Get hi and lo part */
	in_hi = (uint32)((uint64)input >> 32) & MASK_32_BITS;
	in_lo = (uint32)(input & MASK_32_BITS);

	if (shift_amt < 32) {
		/* Extract bit which belongs to hi part after shifting */
		mask = ((uint32)~0) << (32 - shift_amt);
		masked_lo = (in_lo & mask) >> (32 - shift_amt);

		/* Shift hi and lo and prepare output */
		in_hi = (in_hi << shift_amt) | masked_lo;
		in_lo = in_lo << shift_amt;
	} else {
		/* Extract bit which belongs to hi part after shifting */
		shift_amt = shift_amt - 32;

		/* Shift hi and lo and prepare output */
		in_hi = in_lo << shift_amt;
		in_lo = 0;
	}

	shl_out = (((uint64)in_hi << 32) | in_lo);
	return shl_out;
}

/* Does right shift of unsigned 64 bit number */
uint64
bcm_shr_64(uint64 input, uint8 shift_amt)
{
	uint32 in_hi, in_lo;
	uint32 masked_hi = 0;
	uint32 mask;
	uint64 shr_out;

	if (shift_amt == 0) {
		return input;
	}

	/* Get hi and lo part */
	in_hi = (uint32)((uint64)input >> 32) & MASK_32_BITS;
	in_lo = (uint32)(input & MASK_32_BITS);

	if (shift_amt < 32) {
		/* Extract bit which belongs to lo part after shifting */
		mask = (1 << shift_amt) - 1;
		masked_hi = in_hi & mask;

		/* Shift hi and lo and prepare output */
		in_hi = (uint32)in_hi >> shift_amt;
		in_lo = ((uint32)in_lo >> shift_amt) | (masked_hi << (32 - shift_amt));
	} else {
		shift_amt = shift_amt - 32;
		in_lo = in_hi >> shift_amt;
		in_hi = 0;
	}

	shr_out = (((uint64)in_hi << 32) | in_lo);
	return shr_out;
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

#if defined(BCMDRIVER) && !defined(_CFEZ_)
void
dll_pool_detach(void * osh, dll_pool_t * pool, uint16 elems_max, uint16 elem_size)
{
	uint32 mem_size;
	mem_size = sizeof(dll_pool_t) + (elems_max * elem_size);
	if (pool)
		MFREE(osh, pool, mem_size);
}
dll_pool_t *
dll_pool_init(void * osh, uint16 elems_max, uint16 elem_size)
{
	uint32 mem_size, i;
	dll_pool_t * dll_pool_p;
	dll_t * elem_p;

	ASSERT(elem_size > sizeof(dll_t));

	mem_size = sizeof(dll_pool_t) + (elems_max * elem_size);

	if ((dll_pool_p = (dll_pool_t *)MALLOCZ(osh, mem_size)) == NULL) {
		printf("dll_pool_init: elems_max<%u> elem_size<%u> malloc failure\n",
			elems_max, elem_size);
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
dll_pool_free(dll_pool_t * dll_pool_p, void * elem_p)
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

#endif 

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
ipv4_hdr_cksum(uint8 *ip, int ip_len)
{
	uint32 sum = 0;
	uint8 *ptr = ip;

	ASSERT(ip != NULL);
	ASSERT(ip_len >= IPV4_MIN_HEADER_LEN);

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
