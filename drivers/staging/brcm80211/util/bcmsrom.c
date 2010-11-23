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
#include <linux/kernel.h>
#include <linux/string.h>
#include <bcmdefs.h>
#include <osl.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <stdarg.h>
#include <bcmutils.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <bcmdevs.h>
#include <bcmendian.h>
#include <pcicfg.h>
#include <siutils.h>
#include <bcmsrom.h>
#include <bcmsrom_tbl.h>
#ifdef BCMSDIO
#include <bcmsdh.h>
#include <sdio.h>
#endif

#include <bcmnvram.h>
#include <bcmotp.h>

#if defined(BCMSDIO)
#include <sbsdio.h>
#include <sbhnddma.h>
#include <sbsdpcmdev.h>
#endif

#include <proto/ethernet.h>	/* for sprom content groking */

#define	BS_ERROR(args)

#define SROM_OFFSET(sih) ((sih->ccrev > 31) ? \
	(((sih->cccaps & CC_CAP_SROM) == 0) ? NULL : \
	 ((u8 *)curmap + PCI_16KB0_CCREGS_OFFSET + CC_SROM_OTP)) : \
	((u8 *)curmap + PCI_BAR0_SPROM_OFFSET))

#if defined(BCMDBG)
#define WRITE_ENABLE_DELAY	500	/* 500 ms after write enable/disable toggle */
#define WRITE_WORD_DELAY	20	/* 20 ms between each word write */
#endif

typedef struct varbuf {
	char *base;		/* pointer to buffer base */
	char *buf;		/* pointer to current position */
	unsigned int size;	/* current (residual) size in bytes */
} varbuf_t;
extern char *_vars;
extern uint _varsz;

#define SROM_CIS_SINGLE	1

static int initvars_srom_si(si_t *sih, struct osl_info *osh, void *curmap,
			    char **vars, uint *count);
static void _initvars_srom_pci(u8 sromrev, u16 *srom, uint off,
			       varbuf_t *b);
static int initvars_srom_pci(si_t *sih, void *curmap, char **vars,
			     uint *count);
static int initvars_flash_si(si_t *sih, char **vars, uint *count);
#ifdef BCMSDIO
static int initvars_cis_sdio(struct osl_info *osh, char **vars, uint *count);
static int sprom_cmd_sdio(struct osl_info *osh, u8 cmd);
static int sprom_read_sdio(struct osl_info *osh, u16 addr, u16 *data);
#endif				/* BCMSDIO */
static int sprom_read_pci(struct osl_info *osh, si_t *sih, u16 *sprom,
			  uint wordoff, u16 *buf, uint nwords, bool check_crc);
#if defined(BCMNVRAMR)
static int otp_read_pci(struct osl_info *osh, si_t *sih, u16 *buf, uint bufsz);
#endif
static u16 srom_cc_cmd(si_t *sih, struct osl_info *osh, void *ccregs, u32 cmd,
			  uint wordoff, u16 data);

static int initvars_table(struct osl_info *osh, char *start, char *end,
			  char **vars, uint *count);
static int initvars_flash(si_t *sih, struct osl_info *osh, char **vp,
			  uint len);

/* Initialization of varbuf structure */
static void varbuf_init(varbuf_t *b, char *buf, uint size)
{
	b->size = size;
	b->base = b->buf = buf;
}

/* append a null terminated var=value string */
static int varbuf_append(varbuf_t *b, const char *fmt, ...)
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
	s = strchr(b->buf, '=');
	if (s != NULL) {
		len = (size_t) (s - b->buf);
		for (s = b->base; s < b->buf;) {
			if ((bcmp(s, b->buf, len) == 0) && s[len] == '=') {
				len = strlen(s) + 1;
				memmove(s, (s + len),
					((b->buf + r + 1) - (s + len)));
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

/*
 * Initialize local vars from the right source for this platform.
 * Return 0 on success, nonzero on error.
 */
int srom_var_init(si_t *sih, uint bustype, void *curmap, struct osl_info *osh,
		  char **vars, uint *count)
{
	uint len;

	len = 0;

	ASSERT(bustype == bustype);
	if (vars == NULL || count == NULL)
		return 0;

	*vars = NULL;
	*count = 0;

	switch (bustype) {
	case SI_BUS:
	case JTAG_BUS:
		return initvars_srom_si(sih, osh, curmap, vars, count);

	case PCI_BUS:
		ASSERT(curmap != NULL);
		if (curmap == NULL)
			return -1;

		return initvars_srom_pci(sih, curmap, vars, count);

#ifdef BCMSDIO
	case SDIO_BUS:
		return initvars_cis_sdio(osh, vars, count);
#endif				/* BCMSDIO */

	default:
		ASSERT(0);
	}
	return -1;
}

/* support only 16-bit word read from srom */
int
srom_read(si_t *sih, uint bustype, void *curmap, struct osl_info *osh,
	  uint byteoff, uint nbytes, u16 *buf, bool check_crc)
{
	uint off, nw;
#ifdef BCMSDIO
	uint i;
#endif				/* BCMSDIO */

	ASSERT(bustype == bustype);

	/* check input - 16-bit access only */
	if (byteoff & 1 || nbytes & 1 || (byteoff + nbytes) > SROM_MAX)
		return 1;

	off = byteoff / 2;
	nw = nbytes / 2;

	if (bustype == PCI_BUS) {
		if (!curmap)
			return 1;

		if (si_is_sprom_available(sih)) {
			u16 *srom;

			srom = (u16 *) SROM_OFFSET(sih);
			if (srom == NULL)
				return 1;

			if (sprom_read_pci
			    (osh, sih, srom, off, buf, nw, check_crc))
				return 1;
		}
#if defined(BCMNVRAMR)
		else {
			if (otp_read_pci(osh, sih, buf, SROM_MAX))
				return 1;
		}
#endif
#ifdef BCMSDIO
	} else if (bustype == SDIO_BUS) {
		off = byteoff / 2;
		nw = nbytes / 2;
		for (i = 0; i < nw; i++) {
			if (sprom_read_sdio
			    (osh, (u16) (off + i), (u16 *) (buf + i)))
				return 1;
		}
#endif				/* BCMSDIO */
	} else if (bustype == SI_BUS) {
		return 1;
	} else {
		return 1;
	}

	return 0;
}

static const char vstr_manf[] = "manf=%s";
static const char vstr_productname[] = "productname=%s";
static const char vstr_manfid[] = "manfid=0x%x";
static const char vstr_prodid[] = "prodid=0x%x";
#ifdef BCMSDIO
static const char vstr_sdmaxspeed[] = "sdmaxspeed=%d";
static const char vstr_sdmaxblk[][13] = {
"sdmaxblk0=%d", "sdmaxblk1=%d", "sdmaxblk2=%d"};
#endif
static const char vstr_regwindowsz[] = "regwindowsz=%d";
static const char vstr_sromrev[] = "sromrev=%d";
static const char vstr_chiprev[] = "chiprev=%d";
static const char vstr_subvendid[] = "subvendid=0x%x";
static const char vstr_subdevid[] = "subdevid=0x%x";
static const char vstr_boardrev[] = "boardrev=0x%x";
static const char vstr_aa2g[] = "aa2g=0x%x";
static const char vstr_aa5g[] = "aa5g=0x%x";
static const char vstr_ag[] = "ag%d=0x%x";
static const char vstr_cc[] = "cc=%d";
static const char vstr_opo[] = "opo=%d";
static const char vstr_pa0b[][9] = {
"pa0b0=%d", "pa0b1=%d", "pa0b2=%d"};

static const char vstr_pa0itssit[] = "pa0itssit=%d";
static const char vstr_pa0maxpwr[] = "pa0maxpwr=%d";
static const char vstr_pa1b[][9] = {
"pa1b0=%d", "pa1b1=%d", "pa1b2=%d"};

static const char vstr_pa1lob[][11] = {
"pa1lob0=%d", "pa1lob1=%d", "pa1lob2=%d"};

static const char vstr_pa1hib[][11] = {
"pa1hib0=%d", "pa1hib1=%d", "pa1hib2=%d"};

static const char vstr_pa1itssit[] = "pa1itssit=%d";
static const char vstr_pa1maxpwr[] = "pa1maxpwr=%d";
static const char vstr_pa1lomaxpwr[] = "pa1lomaxpwr=%d";
static const char vstr_pa1himaxpwr[] = "pa1himaxpwr=%d";
static const char vstr_oem[] =
    "oem=%02x%02x%02x%02x%02x%02x%02x%02x";
static const char vstr_boardflags[] = "boardflags=0x%x";
static const char vstr_boardflags2[] = "boardflags2=0x%x";
static const char vstr_ledbh[] = "ledbh%d=0x%x";
static const char vstr_noccode[] = "ccode=0x0";
static const char vstr_ccode[] = "ccode=%c%c";
static const char vstr_cctl[] = "cctl=0x%x";
static const char vstr_cckpo[] = "cckpo=0x%x";
static const char vstr_ofdmpo[] = "ofdmpo=0x%x";
static const char vstr_rdlid[] = "rdlid=0x%x";
static const char vstr_rdlrndis[] = "rdlrndis=%d";
static const char vstr_rdlrwu[] = "rdlrwu=%d";
static const char vstr_usbfs[] = "usbfs=%d";
static const char vstr_wpsgpio[] = "wpsgpio=%d";
static const char vstr_wpsled[] = "wpsled=%d";
static const char vstr_rdlsn[] = "rdlsn=%d";
static const char vstr_rssismf2g[] = "rssismf2g=%d";
static const char vstr_rssismc2g[] = "rssismc2g=%d";
static const char vstr_rssisav2g[] = "rssisav2g=%d";
static const char vstr_bxa2g[] = "bxa2g=%d";
static const char vstr_rssismf5g[] = "rssismf5g=%d";
static const char vstr_rssismc5g[] = "rssismc5g=%d";
static const char vstr_rssisav5g[] = "rssisav5g=%d";
static const char vstr_bxa5g[] = "bxa5g=%d";
static const char vstr_tri2g[] = "tri2g=%d";
static const char vstr_tri5gl[] = "tri5gl=%d";
static const char vstr_tri5g[] = "tri5g=%d";
static const char vstr_tri5gh[] = "tri5gh=%d";
static const char vstr_rxpo2g[] = "rxpo2g=%d";
static const char vstr_rxpo5g[] = "rxpo5g=%d";
static const char vstr_boardtype[] = "boardtype=0x%x";
static const char vstr_leddc[] = "leddc=0x%04x";
static const char vstr_vendid[] = "vendid=0x%x";
static const char vstr_devid[] = "devid=0x%x";
static const char vstr_xtalfreq[] = "xtalfreq=%d";
static const char vstr_txchain[] = "txchain=0x%x";
static const char vstr_rxchain[] = "rxchain=0x%x";
static const char vstr_antswitch[] = "antswitch=0x%x";
static const char vstr_regrev[] = "regrev=0x%x";
static const char vstr_antswctl2g[] = "antswctl2g=0x%x";
static const char vstr_triso2g[] = "triso2g=0x%x";
static const char vstr_pdetrange2g[] = "pdetrange2g=0x%x";
static const char vstr_extpagain2g[] = "extpagain2g=0x%x";
static const char vstr_tssipos2g[] = "tssipos2g=0x%x";
static const char vstr_antswctl5g[] = "antswctl5g=0x%x";
static const char vstr_triso5g[] = "triso5g=0x%x";
static const char vstr_pdetrange5g[] = "pdetrange5g=0x%x";
static const char vstr_extpagain5g[] = "extpagain5g=0x%x";
static const char vstr_tssipos5g[] = "tssipos5g=0x%x";
static const char vstr_maxp2ga0[] = "maxp2ga0=0x%x";
static const char vstr_itt2ga0[] = "itt2ga0=0x%x";
static const char vstr_pa[] = "pa%dgw%da%d=0x%x";
static const char vstr_pahl[] = "pa%dg%cw%da%d=0x%x";
static const char vstr_maxp5ga0[] = "maxp5ga0=0x%x";
static const char vstr_itt5ga0[] = "itt5ga0=0x%x";
static const char vstr_maxp5gha0[] = "maxp5gha0=0x%x";
static const char vstr_maxp5gla0[] = "maxp5gla0=0x%x";
static const char vstr_maxp2ga1[] = "maxp2ga1=0x%x";
static const char vstr_itt2ga1[] = "itt2ga1=0x%x";
static const char vstr_maxp5ga1[] = "maxp5ga1=0x%x";
static const char vstr_itt5ga1[] = "itt5ga1=0x%x";
static const char vstr_maxp5gha1[] = "maxp5gha1=0x%x";
static const char vstr_maxp5gla1[] = "maxp5gla1=0x%x";
static const char vstr_cck2gpo[] = "cck2gpo=0x%x";
static const char vstr_ofdm2gpo[] = "ofdm2gpo=0x%x";
static const char vstr_ofdm5gpo[] = "ofdm5gpo=0x%x";
static const char vstr_ofdm5glpo[] = "ofdm5glpo=0x%x";
static const char vstr_ofdm5ghpo[] = "ofdm5ghpo=0x%x";
static const char vstr_cddpo[] = "cddpo=0x%x";
static const char vstr_stbcpo[] = "stbcpo=0x%x";
static const char vstr_bw40po[] = "bw40po=0x%x";
static const char vstr_bwduppo[] = "bwduppo=0x%x";
static const char vstr_mcspo[] = "mcs%dgpo%d=0x%x";
static const char vstr_mcspohl[] = "mcs%dg%cpo%d=0x%x";
static const char vstr_custom[] = "customvar%d=0x%x";
static const char vstr_cckdigfilttype[] = "cckdigfilttype=%d";
static const char vstr_boardnum[] = "boardnum=%d";
static const char vstr_macaddr[] = "macaddr=%s";
static const char vstr_usbepnum[] = "usbepnum=0x%x";
static const char vstr_end[] = "END\0";

u8 patch_pair;

/* For dongle HW, accept partial calibration parameters */
#define BCMDONGLECASE(n)

int srom_parsecis(struct osl_info *osh, u8 *pcis[], uint ciscnt, char **vars,
		  uint *count)
{
	char eabuf[32];
	char *base;
	varbuf_t b;
	u8 *cis, tup, tlen, sromrev = 1;
	int i, j;
	bool ag_init = false;
	u32 w32;
	uint funcid;
	uint cisnum;
	s32 boardnum;
	int err;
	bool standard_cis;

	ASSERT(vars != NULL);
	ASSERT(count != NULL);

	boardnum = -1;

	base = kmalloc(MAXSZ_NVRAM_VARS, GFP_ATOMIC);
	ASSERT(base != NULL);
	if (!base)
		return -2;

	varbuf_init(&b, base, MAXSZ_NVRAM_VARS);
	bzero(base, MAXSZ_NVRAM_VARS);
	eabuf[0] = '\0';
	for (cisnum = 0; cisnum < ciscnt; cisnum++) {
		cis = *pcis++;
		i = 0;
		funcid = 0;
		standard_cis = true;
		do {
			if (standard_cis) {
				tup = cis[i++];
				if (tup == CISTPL_NULL || tup == CISTPL_END)
					tlen = 0;
				else
					tlen = cis[i++];
			} else {
				if (cis[i] == CISTPL_NULL
				    || cis[i] == CISTPL_END) {
					tlen = 0;
					tup = cis[i];
				} else {
					tlen = cis[i];
					tup = CISTPL_BRCM_HNBU;
				}
				++i;
			}
			if ((i + tlen) >= CIS_SIZE)
				break;

			switch (tup) {
			case CISTPL_VERS_1:
				/* assume the strings are good if the version field checks out */
				if (((cis[i + 1] << 8) + cis[i]) >= 0x0008) {
					varbuf_append(&b, vstr_manf,
						      &cis[i + 2]);
					varbuf_append(&b, vstr_productname,
						      &cis[i + 3 +
							   strlen((char *)
								  &cis[i +
								       2])]);
					break;
				}

			case CISTPL_MANFID:
				varbuf_append(&b, vstr_manfid,
					      (cis[i + 1] << 8) + cis[i]);
				varbuf_append(&b, vstr_prodid,
					      (cis[i + 3] << 8) + cis[i + 2]);
				break;

			case CISTPL_FUNCID:
				funcid = cis[i];
				break;

			case CISTPL_FUNCE:
				switch (funcid) {
				case CISTPL_FID_SDIO:
#ifdef BCMSDIO
					if (cis[i] == 0) {
						u8 spd = cis[i + 3];
						static int base[] = {
							-1, 10, 12, 13, 15, 20,
							    25, 30,
							35, 40, 45, 50, 55, 60,
							    70, 80
						};
						static int mult[] = {
							10, 100, 1000, 10000,
							-1, -1, -1, -1
						};
						ASSERT((mult[spd & 0x7] != -1)
						       &&
						       (base
							[(spd >> 3) & 0x0f]));
						varbuf_append(&b,
							      vstr_sdmaxblk[0],
							      (cis[i + 2] << 8)
							      + cis[i + 1]);
						varbuf_append(&b,
							      vstr_sdmaxspeed,
							      (mult[spd & 0x7] *
							       base[(spd >> 3) &
								    0x0f]));
					} else if (cis[i] == 1) {
						varbuf_append(&b,
							      vstr_sdmaxblk
							      [cisnum],
							      (cis[i + 13] << 8)
							      | cis[i + 12]);
					}
#endif				/* BCMSDIO */
					funcid = 0;
					break;
				default:
					/* set macaddr if HNBU_MACADDR not seen yet */
					if (eabuf[0] == '\0'
					    && cis[i] == LAN_NID
					    && !(ETHER_ISNULLADDR(&cis[i + 2]))
					    && !(ETHER_ISMULTI(&cis[i + 2]))) {
						ASSERT(cis[i + 1] ==
						       ETHER_ADDR_LEN);
						snprintf(eabuf, sizeof(eabuf),
							"%pM", &cis[i + 2]);

						/* set boardnum if HNBU_BOARDNUM not seen yet */
						if (boardnum == -1)
							boardnum =
							    (cis[i + 6] << 8) +
							    cis[i + 7];
					}
					break;
				}
				break;

			case CISTPL_CFTABLE:
				varbuf_append(&b, vstr_regwindowsz,
					      (cis[i + 7] << 8) | cis[i + 6]);
				break;

			case CISTPL_BRCM_HNBU:
				switch (cis[i]) {
				case HNBU_SROMREV:
					sromrev = cis[i + 1];
					varbuf_append(&b, vstr_sromrev,
						      sromrev);
					break;

				case HNBU_XTALFREQ:
					varbuf_append(&b, vstr_xtalfreq,
						      (cis[i + 4] << 24) |
						      (cis[i + 3] << 16) |
						      (cis[i + 2] << 8) |
						      cis[i + 1]);
					break;

				case HNBU_CHIPID:
					varbuf_append(&b, vstr_vendid,
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					varbuf_append(&b, vstr_devid,
						      (cis[i + 4] << 8) +
						      cis[i + 3]);
					if (tlen >= 7) {
						varbuf_append(&b, vstr_chiprev,
							      (cis[i + 6] << 8)
							      + cis[i + 5]);
					}
					if (tlen >= 9) {
						varbuf_append(&b,
							      vstr_subvendid,
							      (cis[i + 8] << 8)
							      + cis[i + 7]);
					}
					if (tlen >= 11) {
						varbuf_append(&b, vstr_subdevid,
							      (cis[i + 10] << 8)
							      + cis[i + 9]);
						/* subdevid doubles for boardtype */
						varbuf_append(&b,
							      vstr_boardtype,
							      (cis[i + 10] << 8)
							      + cis[i + 9]);
					}
					break;

				case HNBU_BOARDNUM:
					boardnum =
					    (cis[i + 2] << 8) + cis[i + 1];
					break;

				case HNBU_PATCH:
					{
						char vstr_paddr[16];
						char vstr_pdata[16];

						/* retrieve the patch pairs
						 * from tlen/6; where 6 is
						 * sizeof(patch addr(2)) +
						 * sizeof(patch data(4)).
						 */
						patch_pair = tlen / 6;

						for (j = 0; j < patch_pair; j++) {
							snprintf(vstr_paddr,
								 sizeof
								 (vstr_paddr),
								 "pa%d=0x%%x",
								 j);
							snprintf(vstr_pdata,
								 sizeof
								 (vstr_pdata),
								 "pd%d=0x%%x",
								 j);

							varbuf_append(&b,
								      vstr_paddr,
								      (cis
								       [i +
									(j *
									 6) +
									2] << 8)
								      | cis[i +
									    (j *
									     6)
									    +
									    1]);

							varbuf_append(&b,
								      vstr_pdata,
								      (cis
								       [i +
									(j *
									 6) +
									6] <<
								       24) |
								      (cis
								       [i +
									(j *
									 6) +
									5] <<
								       16) |
								      (cis
								       [i +
									(j *
									 6) +
									4] << 8)
								      | cis[i +
									    (j *
									     6)
									    +
									    3]);
						}
					}
					break;

				case HNBU_BOARDREV:
					if (tlen == 2)
						varbuf_append(&b, vstr_boardrev,
							      cis[i + 1]);
					else
						varbuf_append(&b, vstr_boardrev,
							      (cis[i + 2] << 8)
							      + cis[i + 1]);
					break;

				case HNBU_BOARDFLAGS:
					w32 = (cis[i + 2] << 8) + cis[i + 1];
					if (tlen >= 5)
						w32 |=
						    ((cis[i + 4] << 24) +
						     (cis[i + 3] << 16));
					varbuf_append(&b, vstr_boardflags, w32);

					if (tlen >= 7) {
						w32 =
						    (cis[i + 6] << 8) + cis[i +
									    5];
						if (tlen >= 9)
							w32 |=
							    ((cis[i + 8] << 24)
							     +
							     (cis[i + 7] <<
							      16));
						varbuf_append(&b,
							      vstr_boardflags2,
							      w32);
					}
					break;

				case HNBU_USBFS:
					varbuf_append(&b, vstr_usbfs,
						      cis[i + 1]);
					break;

				case HNBU_BOARDTYPE:
					varbuf_append(&b, vstr_boardtype,
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					break;

				case HNBU_HNBUCIS:
					/*
					 * what follows is a nonstandard HNBU CIS
					 * that lacks CISTPL_BRCM_HNBU tags
					 *
					 * skip 0xff (end of standard CIS)
					 * after this tuple
					 */
					tlen++;
					standard_cis = false;
					break;

				case HNBU_USBEPNUM:
					varbuf_append(&b, vstr_usbepnum,
						      (cis[i + 2] << 8) | cis[i
									      +
									      1]);
					break;

				case HNBU_AA:
					varbuf_append(&b, vstr_aa2g,
						      cis[i + 1]);
					if (tlen >= 3)
						varbuf_append(&b, vstr_aa5g,
							      cis[i + 2]);
					break;

				case HNBU_AG:
					varbuf_append(&b, vstr_ag, 0,
						      cis[i + 1]);
					if (tlen >= 3)
						varbuf_append(&b, vstr_ag, 1,
							      cis[i + 2]);
					if (tlen >= 4)
						varbuf_append(&b, vstr_ag, 2,
							      cis[i + 3]);
					if (tlen >= 5)
						varbuf_append(&b, vstr_ag, 3,
							      cis[i + 4]);
					ag_init = true;
					break;

				case HNBU_ANT5G:
					varbuf_append(&b, vstr_aa5g,
						      cis[i + 1]);
					varbuf_append(&b, vstr_ag, 1,
						      cis[i + 2]);
					break;

				case HNBU_CC:
					ASSERT(sromrev == 1);
					varbuf_append(&b, vstr_cc, cis[i + 1]);
					break;

				case HNBU_PAPARMS:
					switch (tlen) {
					case 2:
						ASSERT(sromrev == 1);
						varbuf_append(&b,
							      vstr_pa0maxpwr,
							      cis[i + 1]);
						break;
					case 10:
						ASSERT(sromrev >= 2);
						varbuf_append(&b, vstr_opo,
							      cis[i + 9]);
						/* FALLTHROUGH */
					case 9:
						varbuf_append(&b,
							      vstr_pa0maxpwr,
							      cis[i + 8]);
						/* FALLTHROUGH */
						BCMDONGLECASE(8)
						    varbuf_append(&b,
								  vstr_pa0itssit,
								  cis[i + 7]);
						/* FALLTHROUGH */
						BCMDONGLECASE(7)
						    for (j = 0; j < 3; j++) {
							varbuf_append(&b,
								      vstr_pa0b
								      [j],
								      (cis
								       [i +
									(j *
									 2) +
									2] << 8)
								      + cis[i +
									    (j *
									     2)
									    +
									    1]);
						}
						break;
					default:
						ASSERT((tlen == 2)
						       || (tlen == 9)
						       || (tlen == 10));
						break;
					}
					break;

				case HNBU_PAPARMS5G:
					ASSERT((sromrev == 2)
					       || (sromrev == 3));
					switch (tlen) {
					case 23:
						varbuf_append(&b,
							      vstr_pa1himaxpwr,
							      cis[i + 22]);
						varbuf_append(&b,
							      vstr_pa1lomaxpwr,
							      cis[i + 21]);
						varbuf_append(&b,
							      vstr_pa1maxpwr,
							      cis[i + 20]);
						/* FALLTHROUGH */
					case 20:
						varbuf_append(&b,
							      vstr_pa1itssit,
							      cis[i + 19]);
						/* FALLTHROUGH */
					case 19:
						for (j = 0; j < 3; j++) {
							varbuf_append(&b,
								      vstr_pa1b
								      [j],
								      (cis
								       [i +
									(j *
									 2) +
									2] << 8)
								      + cis[i +
									    (j *
									     2)
									    +
									    1]);
						}
						for (j = 3; j < 6; j++) {
							varbuf_append(&b,
								      vstr_pa1lob
								      [j - 3],
								      (cis
								       [i +
									(j *
									 2) +
									2] << 8)
								      + cis[i +
									    (j *
									     2)
									    +
									    1]);
						}
						for (j = 6; j < 9; j++) {
							varbuf_append(&b,
								      vstr_pa1hib
								      [j - 6],
								      (cis
								       [i +
									(j *
									 2) +
									2] << 8)
								      + cis[i +
									    (j *
									     2)
									    +
									    1]);
						}
						break;
					default:
						ASSERT((tlen == 19) ||
						       (tlen == 20)
						       || (tlen == 23));
						break;
					}
					break;

				case HNBU_OEM:
					ASSERT(sromrev == 1);
					varbuf_append(&b, vstr_oem,
						      cis[i + 1], cis[i + 2],
						      cis[i + 3], cis[i + 4],
						      cis[i + 5], cis[i + 6],
						      cis[i + 7], cis[i + 8]);
					break;

				case HNBU_LEDS:
					for (j = 1; j <= 4; j++) {
						if (cis[i + j] != 0xff) {
							varbuf_append(&b,
								      vstr_ledbh,
								      j - 1,
								      cis[i +
									  j]);
						}
					}
					break;

				case HNBU_CCODE:
					ASSERT(sromrev > 1);
					if ((cis[i + 1] == 0)
					    || (cis[i + 2] == 0))
						varbuf_append(&b, vstr_noccode);
					else
						varbuf_append(&b, vstr_ccode,
							      cis[i + 1],
							      cis[i + 2]);
					varbuf_append(&b, vstr_cctl,
						      cis[i + 3]);
					break;

				case HNBU_CCKPO:
					ASSERT(sromrev > 2);
					varbuf_append(&b, vstr_cckpo,
						      (cis[i + 2] << 8) | cis[i
									      +
									      1]);
					break;

				case HNBU_OFDMPO:
					ASSERT(sromrev > 2);
					varbuf_append(&b, vstr_ofdmpo,
						      (cis[i + 4] << 24) |
						      (cis[i + 3] << 16) |
						      (cis[i + 2] << 8) |
						      cis[i + 1]);
					break;

				case HNBU_WPS:
					varbuf_append(&b, vstr_wpsgpio,
						      cis[i + 1]);
					if (tlen >= 3)
						varbuf_append(&b, vstr_wpsled,
							      cis[i + 2]);
					break;

				case HNBU_RSSISMBXA2G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rssismf2g,
						      cis[i + 1] & 0xf);
					varbuf_append(&b, vstr_rssismc2g,
						      (cis[i + 1] >> 4) & 0xf);
					varbuf_append(&b, vstr_rssisav2g,
						      cis[i + 2] & 0x7);
					varbuf_append(&b, vstr_bxa2g,
						      (cis[i + 2] >> 3) & 0x3);
					break;

				case HNBU_RSSISMBXA5G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rssismf5g,
						      cis[i + 1] & 0xf);
					varbuf_append(&b, vstr_rssismc5g,
						      (cis[i + 1] >> 4) & 0xf);
					varbuf_append(&b, vstr_rssisav5g,
						      cis[i + 2] & 0x7);
					varbuf_append(&b, vstr_bxa5g,
						      (cis[i + 2] >> 3) & 0x3);
					break;

				case HNBU_TRI2G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_tri2g,
						      cis[i + 1]);
					break;

				case HNBU_TRI5G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_tri5gl,
						      cis[i + 1]);
					varbuf_append(&b, vstr_tri5g,
						      cis[i + 2]);
					varbuf_append(&b, vstr_tri5gh,
						      cis[i + 3]);
					break;

				case HNBU_RXPO2G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rxpo2g,
						      cis[i + 1]);
					break;

				case HNBU_RXPO5G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rxpo5g,
						      cis[i + 1]);
					break;

				case HNBU_MACADDR:
					if (!(ETHER_ISNULLADDR(&cis[i + 1])) &&
					    !(ETHER_ISMULTI(&cis[i + 1]))) {
						snprintf(eabuf, sizeof(eabuf),
							"%pM", &cis[i + 1]);

						/* set boardnum if HNBU_BOARDNUM not seen yet */
						if (boardnum == -1)
							boardnum =
							    (cis[i + 5] << 8) +
							    cis[i + 6];
					}
					break;

				case HNBU_LEDDC:
					/* CIS leddc only has 16bits, convert it to 32bits */
					w32 = ((cis[i + 2] << 24) |	/* oncount */
					       (cis[i + 1] << 8));	/* offcount */
					varbuf_append(&b, vstr_leddc, w32);
					break;

				case HNBU_CHAINSWITCH:
					varbuf_append(&b, vstr_txchain,
						      cis[i + 1]);
					varbuf_append(&b, vstr_rxchain,
						      cis[i + 2]);
					varbuf_append(&b, vstr_antswitch,
						      (cis[i + 4] << 8) +
						      cis[i + 3]);
					break;

				case HNBU_REGREV:
					varbuf_append(&b, vstr_regrev,
						      cis[i + 1]);
					break;

				case HNBU_FEM:{
						u16 fem =
						    (cis[i + 2] << 8) + cis[i +
									    1];
						varbuf_append(&b,
							      vstr_antswctl2g,
							      (fem &
							       SROM8_FEM_ANTSWLUT_MASK)
							      >>
							      SROM8_FEM_ANTSWLUT_SHIFT);
						varbuf_append(&b, vstr_triso2g,
							      (fem &
							       SROM8_FEM_TR_ISO_MASK)
							      >>
							      SROM8_FEM_TR_ISO_SHIFT);
						varbuf_append(&b,
							      vstr_pdetrange2g,
							      (fem &
							       SROM8_FEM_PDET_RANGE_MASK)
							      >>
							      SROM8_FEM_PDET_RANGE_SHIFT);
						varbuf_append(&b,
							      vstr_extpagain2g,
							      (fem &
							       SROM8_FEM_EXTPA_GAIN_MASK)
							      >>
							      SROM8_FEM_EXTPA_GAIN_SHIFT);
						varbuf_append(&b,
							      vstr_tssipos2g,
							      (fem &
							       SROM8_FEM_TSSIPOS_MASK)
							      >>
							      SROM8_FEM_TSSIPOS_SHIFT);
						if (tlen < 5)
							break;

						fem =
						    (cis[i + 4] << 8) + cis[i +
									    3];
						varbuf_append(&b,
							      vstr_antswctl5g,
							      (fem &
							       SROM8_FEM_ANTSWLUT_MASK)
							      >>
							      SROM8_FEM_ANTSWLUT_SHIFT);
						varbuf_append(&b, vstr_triso5g,
							      (fem &
							       SROM8_FEM_TR_ISO_MASK)
							      >>
							      SROM8_FEM_TR_ISO_SHIFT);
						varbuf_append(&b,
							      vstr_pdetrange5g,
							      (fem &
							       SROM8_FEM_PDET_RANGE_MASK)
							      >>
							      SROM8_FEM_PDET_RANGE_SHIFT);
						varbuf_append(&b,
							      vstr_extpagain5g,
							      (fem &
							       SROM8_FEM_EXTPA_GAIN_MASK)
							      >>
							      SROM8_FEM_EXTPA_GAIN_SHIFT);
						varbuf_append(&b,
							      vstr_tssipos5g,
							      (fem &
							       SROM8_FEM_TSSIPOS_MASK)
							      >>
							      SROM8_FEM_TSSIPOS_SHIFT);
						break;
					}

				case HNBU_PAPARMS_C0:
					varbuf_append(&b, vstr_maxp2ga0,
						      cis[i + 1]);
					varbuf_append(&b, vstr_itt2ga0,
						      cis[i + 2]);
					varbuf_append(&b, vstr_pa, 2, 0, 0,
						      (cis[i + 4] << 8) +
						      cis[i + 3]);
					varbuf_append(&b, vstr_pa, 2, 1, 0,
						      (cis[i + 6] << 8) +
						      cis[i + 5]);
					varbuf_append(&b, vstr_pa, 2, 2, 0,
						      (cis[i + 8] << 8) +
						      cis[i + 7]);
					if (tlen < 31)
						break;

					varbuf_append(&b, vstr_maxp5ga0,
						      cis[i + 9]);
					varbuf_append(&b, vstr_itt5ga0,
						      cis[i + 10]);
					varbuf_append(&b, vstr_maxp5gha0,
						      cis[i + 11]);
					varbuf_append(&b, vstr_maxp5gla0,
						      cis[i + 12]);
					varbuf_append(&b, vstr_pa, 5, 0, 0,
						      (cis[i + 14] << 8) +
						      cis[i + 13]);
					varbuf_append(&b, vstr_pa, 5, 1, 0,
						      (cis[i + 16] << 8) +
						      cis[i + 15]);
					varbuf_append(&b, vstr_pa, 5, 2, 0,
						      (cis[i + 18] << 8) +
						      cis[i + 17]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 0,
						      0,
						      (cis[i + 20] << 8) +
						      cis[i + 19]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 1,
						      0,
						      (cis[i + 22] << 8) +
						      cis[i + 21]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 2,
						      0,
						      (cis[i + 24] << 8) +
						      cis[i + 23]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 0,
						      0,
						      (cis[i + 26] << 8) +
						      cis[i + 25]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 1,
						      0,
						      (cis[i + 28] << 8) +
						      cis[i + 27]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 2,
						      0,
						      (cis[i + 30] << 8) +
						      cis[i + 29]);
					break;

				case HNBU_PAPARMS_C1:
					varbuf_append(&b, vstr_maxp2ga1,
						      cis[i + 1]);
					varbuf_append(&b, vstr_itt2ga1,
						      cis[i + 2]);
					varbuf_append(&b, vstr_pa, 2, 0, 1,
						      (cis[i + 4] << 8) +
						      cis[i + 3]);
					varbuf_append(&b, vstr_pa, 2, 1, 1,
						      (cis[i + 6] << 8) +
						      cis[i + 5]);
					varbuf_append(&b, vstr_pa, 2, 2, 1,
						      (cis[i + 8] << 8) +
						      cis[i + 7]);
					if (tlen < 31)
						break;

					varbuf_append(&b, vstr_maxp5ga1,
						      cis[i + 9]);
					varbuf_append(&b, vstr_itt5ga1,
						      cis[i + 10]);
					varbuf_append(&b, vstr_maxp5gha1,
						      cis[i + 11]);
					varbuf_append(&b, vstr_maxp5gla1,
						      cis[i + 12]);
					varbuf_append(&b, vstr_pa, 5, 0, 1,
						      (cis[i + 14] << 8) +
						      cis[i + 13]);
					varbuf_append(&b, vstr_pa, 5, 1, 1,
						      (cis[i + 16] << 8) +
						      cis[i + 15]);
					varbuf_append(&b, vstr_pa, 5, 2, 1,
						      (cis[i + 18] << 8) +
						      cis[i + 17]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 0,
						      1,
						      (cis[i + 20] << 8) +
						      cis[i + 19]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 1,
						      1,
						      (cis[i + 22] << 8) +
						      cis[i + 21]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 2,
						      1,
						      (cis[i + 24] << 8) +
						      cis[i + 23]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 0,
						      1,
						      (cis[i + 26] << 8) +
						      cis[i + 25]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 1,
						      1,
						      (cis[i + 28] << 8) +
						      cis[i + 27]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 2,
						      1,
						      (cis[i + 30] << 8) +
						      cis[i + 29]);
					break;

				case HNBU_PO_CCKOFDM:
					varbuf_append(&b, vstr_cck2gpo,
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					varbuf_append(&b, vstr_ofdm2gpo,
						      (cis[i + 6] << 24) +
						      (cis[i + 5] << 16) +
						      (cis[i + 4] << 8) +
						      cis[i + 3]);
					if (tlen < 19)
						break;

					varbuf_append(&b, vstr_ofdm5gpo,
						      (cis[i + 10] << 24) +
						      (cis[i + 9] << 16) +
						      (cis[i + 8] << 8) +
						      cis[i + 7]);
					varbuf_append(&b, vstr_ofdm5glpo,
						      (cis[i + 14] << 24) +
						      (cis[i + 13] << 16) +
						      (cis[i + 12] << 8) +
						      cis[i + 11]);
					varbuf_append(&b, vstr_ofdm5ghpo,
						      (cis[i + 18] << 24) +
						      (cis[i + 17] << 16) +
						      (cis[i + 16] << 8) +
						      cis[i + 15]);
					break;

				case HNBU_PO_MCS2G:
					for (j = 0; j <= (tlen / 2); j++) {
						varbuf_append(&b, vstr_mcspo, 2,
							      j,
							      (cis
							       [i + 2 +
								2 * j] << 8) +
							      cis[i + 1 +
								  2 * j]);
					}
					break;

				case HNBU_PO_MCS5GM:
					for (j = 0; j <= (tlen / 2); j++) {
						varbuf_append(&b, vstr_mcspo, 5,
							      j,
							      (cis
							       [i + 2 +
								2 * j] << 8) +
							      cis[i + 1 +
								  2 * j]);
					}
					break;

				case HNBU_PO_MCS5GLH:
					for (j = 0; j <= (tlen / 4); j++) {
						varbuf_append(&b, vstr_mcspohl,
							      5, 'l', j,
							      (cis
							       [i + 2 +
								2 * j] << 8) +
							      cis[i + 1 +
								  2 * j]);
					}

					for (j = 0; j <= (tlen / 4); j++) {
						varbuf_append(&b, vstr_mcspohl,
							      5, 'h', j,
							      (cis
							       [i +
								((tlen / 2) +
								 2) +
								2 * j] << 8) +
							      cis[i +
								  ((tlen / 2) +
								   1) + 2 * j]);
					}

					break;

				case HNBU_PO_CDD:
					varbuf_append(&b, vstr_cddpo,
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					break;

				case HNBU_PO_STBC:
					varbuf_append(&b, vstr_stbcpo,
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					break;

				case HNBU_PO_40M:
					varbuf_append(&b, vstr_bw40po,
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					break;

				case HNBU_PO_40MDUP:
					varbuf_append(&b, vstr_bwduppo,
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					break;

				case HNBU_OFDMPO5G:
					varbuf_append(&b, vstr_ofdm5gpo,
						      (cis[i + 4] << 24) +
						      (cis[i + 3] << 16) +
						      (cis[i + 2] << 8) +
						      cis[i + 1]);
					varbuf_append(&b, vstr_ofdm5glpo,
						      (cis[i + 8] << 24) +
						      (cis[i + 7] << 16) +
						      (cis[i + 6] << 8) +
						      cis[i + 5]);
					varbuf_append(&b, vstr_ofdm5ghpo,
						      (cis[i + 12] << 24) +
						      (cis[i + 11] << 16) +
						      (cis[i + 10] << 8) +
						      cis[i + 9]);
					break;

				case HNBU_CUSTOM1:
					varbuf_append(&b, vstr_custom, 1,
						      ((cis[i + 4] << 24) +
						       (cis[i + 3] << 16) +
						       (cis[i + 2] << 8) +
						       cis[i + 1]));
					break;

#if defined(BCMSDIO)
				case HNBU_SROM3SWRGN:
					if (tlen >= 73) {
						u16 srom[35];
						u8 srev = cis[i + 1 + 70];
						ASSERT(srev == 3);
						/* make tuple value 16-bit aligned and parse it */
						bcopy(&cis[i + 1], srom,
						      sizeof(srom));
						_initvars_srom_pci(srev, srom,
								   SROM3_SWRGN_OFF,
								   &b);
						/* 2.4G antenna gain is included in SROM */
						ag_init = true;
						/* Ethernet MAC address is included in SROM */
						eabuf[0] = 0;
						boardnum = -1;
					}
					/* create extra variables */
					if (tlen >= 75)
						varbuf_append(&b, vstr_vendid,
							      (cis[i + 1 + 73]
							       << 8) + cis[i +
									   1 +
									   72]);
					if (tlen >= 77)
						varbuf_append(&b, vstr_devid,
							      (cis[i + 1 + 75]
							       << 8) + cis[i +
									   1 +
									   74]);
					if (tlen >= 79)
						varbuf_append(&b, vstr_xtalfreq,
							      (cis[i + 1 + 77]
							       << 8) + cis[i +
									   1 +
									   76]);
					break;
#endif				/* defined(BCMSDIO) */

				case HNBU_CCKFILTTYPE:
					varbuf_append(&b, vstr_cckdigfilttype,
						      (cis[i + 1]));
					break;
				}

				break;
			}
			i += tlen;
		} while (tup != CISTPL_END);
	}

	if (boardnum != -1) {
		varbuf_append(&b, vstr_boardnum, boardnum);
	}

	if (eabuf[0]) {
		varbuf_append(&b, vstr_macaddr, eabuf);
	}

	/* if there is no antenna gain field, set default */
	if (getvar(NULL, "ag0") == NULL && ag_init == false) {
		varbuf_append(&b, vstr_ag, 0, 0xff);
	}

	/* final nullbyte terminator */
	ASSERT(b.size >= 1);
	*b.buf++ = '\0';

	ASSERT(b.buf - base <= MAXSZ_NVRAM_VARS);
	err = initvars_table(osh, base, b.buf, vars, count);

	kfree(base);
	return err;
}

/* In chips with chipcommon rev 32 and later, the srom is in chipcommon,
 * not in the bus cores.
 */
static u16
srom_cc_cmd(si_t *sih, struct osl_info *osh, void *ccregs, u32 cmd,
	    uint wordoff, u16 data)
{
	chipcregs_t *cc = (chipcregs_t *) ccregs;
	uint wait_cnt = 1000;

	if ((cmd == SRC_OP_READ) || (cmd == SRC_OP_WRITE)) {
		W_REG(osh, &cc->sromaddress, wordoff * 2);
		if (cmd == SRC_OP_WRITE)
			W_REG(osh, &cc->sromdata, data);
	}

	W_REG(osh, &cc->sromcontrol, SRC_START | cmd);

	while (wait_cnt--) {
		if ((R_REG(osh, &cc->sromcontrol) & SRC_BUSY) == 0)
			break;
	}

	if (!wait_cnt) {
		BS_ERROR(("%s: Command 0x%x timed out\n", __func__, cmd));
		return 0xffff;
	}
	if (cmd == SRC_OP_READ)
		return (u16) R_REG(osh, &cc->sromdata);
	else
		return 0xffff;
}

/*
 * Read in and validate sprom.
 * Return 0 on success, nonzero on error.
 */
static int
sprom_read_pci(struct osl_info *osh, si_t *sih, u16 *sprom, uint wordoff,
	       u16 *buf, uint nwords, bool check_crc)
{
	int err = 0;
	uint i;
	void *ccregs = NULL;

	/* read the sprom */
	for (i = 0; i < nwords; i++) {

		if (sih->ccrev > 31 && ISSIM_ENAB(sih)) {
			/* use indirect since direct is too slow on QT */
			if ((sih->cccaps & CC_CAP_SROM) == 0)
				return 1;

			ccregs = (void *)((u8 *) sprom - CC_SROM_OTP);
			buf[i] =
			    srom_cc_cmd(sih, osh, ccregs, SRC_OP_READ,
					wordoff + i, 0);

		} else {
			if (ISSIM_ENAB(sih))
				buf[i] = R_REG(osh, &sprom[wordoff + i]);

			buf[i] = R_REG(osh, &sprom[wordoff + i]);
		}

	}

	/* bypass crc checking for simulation to allow srom hack */
	if (ISSIM_ENAB(sih))
		return err;

	if (check_crc) {

		if (buf[0] == 0xffff) {
			/* The hardware thinks that an srom that starts with 0xffff
			 * is blank, regardless of the rest of the content, so declare
			 * it bad.
			 */
			BS_ERROR(("%s: buf[0] = 0x%x, returning bad-crc\n",
				  __func__, buf[0]));
			return 1;
		}

		/* fixup the endianness so crc8 will pass */
		htol16_buf(buf, nwords * 2);
		if (hndcrc8((u8 *) buf, nwords * 2, CRC8_INIT_VALUE) !=
		    CRC8_GOOD_VALUE) {
			/* DBG only pci always read srom4 first, then srom8/9 */
			/* BS_ERROR(("%s: bad crc\n", __func__)); */
			err = 1;
		}
		/* now correct the endianness of the byte array */
		ltoh16_buf(buf, nwords * 2);
	}
	return err;
}

#if defined(BCMNVRAMR)
static int otp_read_pci(struct osl_info *osh, si_t *sih, u16 *buf, uint bufsz)
{
	u8 *otp;
	uint sz = OTP_SZ_MAX / 2;	/* size in words */
	int err = 0;

	ASSERT(bufsz <= OTP_SZ_MAX);

	otp = kzalloc(OTP_SZ_MAX, GFP_ATOMIC);
	if (otp == NULL) {
		return BCME_ERROR;
	}

	err = otp_read_region(sih, OTP_HW_RGN, (u16 *) otp, &sz);

	bcopy(otp, buf, bufsz);

	if (otp)
		kfree(otp);

	/* Check CRC */
	if (buf[0] == 0xffff) {
		/* The hardware thinks that an srom that starts with 0xffff
		 * is blank, regardless of the rest of the content, so declare
		 * it bad.
		 */
		BS_ERROR(("%s: buf[0] = 0x%x, returning bad-crc\n", __func__,
			  buf[0]));
		return 1;
	}

	/* fixup the endianness so crc8 will pass */
	htol16_buf(buf, bufsz);
	if (hndcrc8((u8 *) buf, SROM4_WORDS * 2, CRC8_INIT_VALUE) !=
	    CRC8_GOOD_VALUE) {
		BS_ERROR(("%s: bad crc\n", __func__));
		err = 1;
	}
	/* now correct the endianness of the byte array */
	ltoh16_buf(buf, bufsz);

	return err;
}
#endif				/* defined(BCMNVRAMR) */
/*
* Create variable table from memory.
* Return 0 on success, nonzero on error.
*/
static int initvars_table(struct osl_info *osh, char *start, char *end,
			  char **vars, uint *count)
{
	int c = (int)(end - start);

	/* do it only when there is more than just the null string */
	if (c > 1) {
		char *vp = kmalloc(c, GFP_ATOMIC);
		ASSERT(vp != NULL);
		if (!vp)
			return BCME_NOMEM;
		bcopy(start, vp, c);
		*vars = vp;
		*count = c;
	} else {
		*vars = NULL;
		*count = 0;
	}

	return 0;
}

/*
 * Find variables with <devpath> from flash. 'base' points to the beginning
 * of the table upon enter and to the end of the table upon exit when success.
 * Return 0 on success, nonzero on error.
 */
static int initvars_flash(si_t *sih, struct osl_info *osh, char **base,
			  uint len)
{
	char *vp = *base;
	char *flash;
	int err;
	char *s;
	uint l, dl, copy_len;
	char devpath[SI_DEVPATH_BUFSZ];

	/* allocate memory and read in flash */
	flash = kmalloc(NVRAM_SPACE, GFP_ATOMIC);
	if (!flash)
		return BCME_NOMEM;
	err = nvram_getall(flash, NVRAM_SPACE);
	if (err)
		goto exit;

	si_devpath(sih, devpath, sizeof(devpath));

	/* grab vars with the <devpath> prefix in name */
	dl = strlen(devpath);
	for (s = flash; s && *s; s += l + 1) {
		l = strlen(s);

		/* skip non-matching variable */
		if (strncmp(s, devpath, dl))
			continue;

		/* is there enough room to copy? */
		copy_len = l - dl + 1;
		if (len < copy_len) {
			err = BCME_BUFTOOSHORT;
			goto exit;
		}

		/* no prefix, just the name=value */
		strncpy(vp, &s[dl], copy_len);
		vp += copy_len;
		len -= copy_len;
	}

	/* add null string as terminator */
	if (len < 1) {
		err = BCME_BUFTOOSHORT;
		goto exit;
	}
	*vp++ = '\0';

	*base = vp;

 exit:	kfree(flash);
	return err;
}

/*
 * Initialize nonvolatile variable table from flash.
 * Return 0 on success, nonzero on error.
 */
static int initvars_flash_si(si_t *sih, char **vars, uint *count)
{
	struct osl_info *osh = si_osh(sih);
	char *vp, *base;
	int err;

	ASSERT(vars != NULL);
	ASSERT(count != NULL);

	base = vp = kmalloc(MAXSZ_NVRAM_VARS, GFP_ATOMIC);
	ASSERT(vp != NULL);
	if (!vp)
		return BCME_NOMEM;

	err = initvars_flash(sih, osh, &vp, MAXSZ_NVRAM_VARS);
	if (err == 0)
		err = initvars_table(osh, base, vp, vars, count);

	kfree(base);

	return err;
}

/* Parse SROM and create name=value pairs. 'srom' points to
 * the SROM word array. 'off' specifies the offset of the
 * first word 'srom' points to, which should be either 0 or
 * SROM3_SWRG_OFF (full SROM or software region).
 */

static uint mask_shift(u16 mask)
{
	uint i;
	for (i = 0; i < (sizeof(mask) << 3); i++) {
		if (mask & (1 << i))
			return i;
	}
	ASSERT(mask);
	return 0;
}

static uint mask_width(u16 mask)
{
	int i;
	for (i = (sizeof(mask) << 3) - 1; i >= 0; i--) {
		if (mask & (1 << i))
			return (uint) (i - mask_shift(mask) + 1);
	}
	ASSERT(mask);
	return 0;
}

#if defined(BCMDBG)
static bool mask_valid(u16 mask)
{
	uint shift = mask_shift(mask);
	uint width = mask_width(mask);
	return mask == ((~0 << shift) & ~(~0 << (shift + width)));
}
#endif				/* BCMDBG */

static void _initvars_srom_pci(u8 sromrev, u16 *srom, uint off, varbuf_t *b)
{
	u16 w;
	u32 val;
	const sromvar_t *srv;
	uint width;
	uint flags;
	u32 sr = (1 << sromrev);

	varbuf_append(b, "sromrev=%d", sromrev);

	for (srv = pci_sromvars; srv->name != NULL; srv++) {
		const char *name;

		if ((srv->revmask & sr) == 0)
			continue;

		if (srv->off < off)
			continue;

		flags = srv->flags;
		name = srv->name;

		/* This entry is for mfgc only. Don't generate param for it, */
		if (flags & SRFL_NOVAR)
			continue;

		if (flags & SRFL_ETHADDR) {
			struct ether_addr ea;

			ea.octet[0] = (srom[srv->off - off] >> 8) & 0xff;
			ea.octet[1] = srom[srv->off - off] & 0xff;
			ea.octet[2] = (srom[srv->off + 1 - off] >> 8) & 0xff;
			ea.octet[3] = srom[srv->off + 1 - off] & 0xff;
			ea.octet[4] = (srom[srv->off + 2 - off] >> 8) & 0xff;
			ea.octet[5] = srom[srv->off + 2 - off] & 0xff;

			varbuf_append(b, "%s=%pM", name, ea.octet);
		} else {
			ASSERT(mask_valid(srv->mask));
			ASSERT(mask_width(srv->mask));

			w = srom[srv->off - off];
			val = (w & srv->mask) >> mask_shift(srv->mask);
			width = mask_width(srv->mask);

			while (srv->flags & SRFL_MORE) {
				srv++;
				ASSERT(srv->name != NULL);

				if (srv->off == 0 || srv->off < off)
					continue;

				ASSERT(mask_valid(srv->mask));
				ASSERT(mask_width(srv->mask));

				w = srom[srv->off - off];
				val +=
				    ((w & srv->mask) >> mask_shift(srv->
								   mask)) <<
				    width;
				width += mask_width(srv->mask);
			}

			if ((flags & SRFL_NOFFS)
			    && ((int)val == (1 << width) - 1))
				continue;

			if (flags & SRFL_CCODE) {
				if (val == 0)
					varbuf_append(b, "ccode=");
				else
					varbuf_append(b, "ccode=%c%c",
						      (val >> 8), (val & 0xff));
			}
			/* LED Powersave duty cycle has to be scaled:
			 *(oncount >> 24) (offcount >> 8)
			 */
			else if (flags & SRFL_LEDDC) {
				u32 w32 = (((val >> 8) & 0xff) << 24) |	/* oncount */
				    (((val & 0xff)) << 8);	/* offcount */
				varbuf_append(b, "leddc=%d", w32);
			} else if (flags & SRFL_PRHEX)
				varbuf_append(b, "%s=0x%x", name, val);
			else if ((flags & SRFL_PRSIGN)
				 && (val & (1 << (width - 1))))
				varbuf_append(b, "%s=%d", name,
					      (int)(val | (~0 << width)));
			else
				varbuf_append(b, "%s=%u", name, val);
		}
	}

	if (sromrev >= 4) {
		/* Do per-path variables */
		uint p, pb, psz;

		if (sromrev >= 8) {
			pb = SROM8_PATH0;
			psz = SROM8_PATH1 - SROM8_PATH0;
		} else {
			pb = SROM4_PATH0;
			psz = SROM4_PATH1 - SROM4_PATH0;
		}

		for (p = 0; p < MAX_PATH_SROM; p++) {
			for (srv = perpath_pci_sromvars; srv->name != NULL;
			     srv++) {
				if ((srv->revmask & sr) == 0)
					continue;

				if (pb + srv->off < off)
					continue;

				/* This entry is for mfgc only. Don't generate param for it, */
				if (srv->flags & SRFL_NOVAR)
					continue;

				w = srom[pb + srv->off - off];

				ASSERT(mask_valid(srv->mask));
				val = (w & srv->mask) >> mask_shift(srv->mask);
				width = mask_width(srv->mask);

				/* Cheating: no per-path var is more than 1 word */

				if ((srv->flags & SRFL_NOFFS)
				    && ((int)val == (1 << width) - 1))
					continue;

				if (srv->flags & SRFL_PRHEX)
					varbuf_append(b, "%s%d=0x%x", srv->name,
						      p, val);
				else
					varbuf_append(b, "%s%d=%d", srv->name,
						      p, val);
			}
			pb += psz;
		}
	}
}

/*
 * Initialize nonvolatile variable table from sprom.
 * Return 0 on success, nonzero on error.
 */
static int initvars_srom_pci(si_t *sih, void *curmap, char **vars, uint *count)
{
	u16 *srom, *sromwindow;
	u8 sromrev = 0;
	u32 sr;
	varbuf_t b;
	char *vp, *base = NULL;
	struct osl_info *osh = si_osh(sih);
	bool flash = false;
	int err = 0;

	/*
	 * Apply CRC over SROM content regardless SROM is present or not,
	 * and use variable <devpath>sromrev's existance in flash to decide
	 * if we should return an error when CRC fails or read SROM variables
	 * from flash.
	 */
	srom = kmalloc(SROM_MAX, GFP_ATOMIC);
	ASSERT(srom != NULL);
	if (!srom)
		return -2;

	sromwindow = (u16 *) SROM_OFFSET(sih);
	if (si_is_sprom_available(sih)) {
		err =
		    sprom_read_pci(osh, sih, sromwindow, 0, srom, SROM_WORDS,
				   true);

		if ((srom[SROM4_SIGN] == SROM4_SIGNATURE) ||
		    (((sih->buscoretype == PCIE_CORE_ID)
		      && (sih->buscorerev >= 6))
		     || ((sih->buscoretype == PCI_CORE_ID)
			 && (sih->buscorerev >= 0xe)))) {
			/* sromrev >= 4, read more */
			err =
			    sprom_read_pci(osh, sih, sromwindow, 0, srom,
					   SROM4_WORDS, true);
			sromrev = srom[SROM4_CRCREV] & 0xff;
			if (err)
				BS_ERROR(("%s: srom %d, bad crc\n", __func__,
					  sromrev));

		} else if (err == 0) {
			/* srom is good and is rev < 4 */
			/* top word of sprom contains version and crc8 */
			sromrev = srom[SROM_CRCREV] & 0xff;
			/* bcm4401 sroms misprogrammed */
			if (sromrev == 0x10)
				sromrev = 1;
		}
	}
#if defined(BCMNVRAMR)
	/* Use OTP if SPROM not available */
	else if ((err = otp_read_pci(osh, sih, srom, SROM_MAX)) == 0) {
		/* OTP only contain SROM rev8/rev9 for now */
		sromrev = srom[SROM4_CRCREV] & 0xff;
	}
#endif
	else {
		err = 1;
		BS_ERROR(("Neither SPROM nor OTP has valid image\n"));
	}

	/* We want internal/wltest driver to come up with default sromvars so we can
	 * program a blank SPROM/OTP.
	 */
	if (err) {
		char *value;
		u32 val;
		val = 0;

		value = si_getdevpathvar(sih, "sromrev");
		if (value) {
			sromrev = (u8) simple_strtoul(value, NULL, 0);
			flash = true;
			goto varscont;
		}

		BS_ERROR(("%s, SROM CRC Error\n", __func__));

		value = si_getnvramflvar(sih, "sromrev");
		if (value) {
			err = 0;
			goto errout;
		}

		{
			err = -1;
			goto errout;
		}
	}

 varscont:
	/* Bitmask for the sromrev */
	sr = 1 << sromrev;

	/* srom version check: Current valid versions: 1, 2, 3, 4, 5, 8, 9 */
	if ((sr & 0x33e) == 0) {
		err = -2;
		goto errout;
	}

	ASSERT(vars != NULL);
	ASSERT(count != NULL);

	base = vp = kmalloc(MAXSZ_NVRAM_VARS, GFP_ATOMIC);
	ASSERT(vp != NULL);
	if (!vp) {
		err = -2;
		goto errout;
	}

	/* read variables from flash */
	if (flash) {
		err = initvars_flash(sih, osh, &vp, MAXSZ_NVRAM_VARS);
		if (err)
			goto errout;
		goto varsdone;
	}

	varbuf_init(&b, base, MAXSZ_NVRAM_VARS);

	/* parse SROM into name=value pairs. */
	_initvars_srom_pci(sromrev, srom, 0, &b);

	/* final nullbyte terminator */
	ASSERT(b.size >= 1);
	vp = b.buf;
	*vp++ = '\0';

	ASSERT((vp - base) <= MAXSZ_NVRAM_VARS);

 varsdone:
	err = initvars_table(osh, base, vp, vars, count);

 errout:
	if (base)
		kfree(base);

	kfree(srom);
	return err;
}

#ifdef BCMSDIO
/*
 * Read the SDIO cis and call parsecis to initialize the vars.
 * Return 0 on success, nonzero on error.
 */
static int initvars_cis_sdio(struct osl_info *osh, char **vars, uint *count)
{
	u8 *cis[SBSDIO_NUM_FUNCTION + 1];
	uint fn, numfn;
	int rc = 0;

	numfn = bcmsdh_query_iofnum(NULL);
	ASSERT(numfn <= SDIOD_MAX_IOFUNCS);

	for (fn = 0; fn <= numfn; fn++) {
		cis[fn] = kzalloc(SBSDIO_CIS_SIZE_LIMIT, GFP_ATOMIC);
		if (cis[fn] == NULL) {
			rc = -1;
			break;
		}

		if (bcmsdh_cis_read(NULL, fn, cis[fn], SBSDIO_CIS_SIZE_LIMIT) !=
		    0) {
			kfree(cis[fn]);
			rc = -2;
			break;
		}
	}

	if (!rc)
		rc = srom_parsecis(osh, cis, fn, vars, count);

	while (fn-- > 0)
		kfree(cis[fn]);

	return rc;
}

/* set SDIO sprom command register */
static int sprom_cmd_sdio(struct osl_info *osh, u8 cmd)
{
	u8 status = 0;
	uint wait_cnt = 1000;

	/* write sprom command register */
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_CS, cmd, NULL);

	/* wait status */
	while (wait_cnt--) {
		status =
		    bcmsdh_cfg_read(NULL, SDIO_FUNC_1, SBSDIO_SPROM_CS, NULL);
		if (status & SBSDIO_SPROM_DONE)
			return 0;
	}

	return 1;
}

/* read a word from the SDIO srom */
static int sprom_read_sdio(struct osl_info *osh, u16 addr, u16 *data)
{
	u8 addr_l, addr_h, data_l, data_h;

	addr_l = (u8) ((addr * 2) & 0xff);
	addr_h = (u8) (((addr * 2) >> 8) & 0xff);

	/* set address */
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_ADDR_HIGH, addr_h,
			 NULL);
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_ADDR_LOW, addr_l,
			 NULL);

	/* do read */
	if (sprom_cmd_sdio(osh, SBSDIO_SPROM_READ))
		return 1;

	/* read data */
	data_h =
	    bcmsdh_cfg_read(NULL, SDIO_FUNC_1, SBSDIO_SPROM_DATA_HIGH, NULL);
	data_l =
	    bcmsdh_cfg_read(NULL, SDIO_FUNC_1, SBSDIO_SPROM_DATA_LOW, NULL);

	*data = (data_h << 8) | data_l;
	return 0;
}
#endif				/* BCMSDIO */

static int initvars_srom_si(si_t *sih, struct osl_info *osh, void *curmap,
			    char **vars, uint *varsz)
{
	/* Search flash nvram section for srom variables */
	return initvars_flash_si(sih, vars, varsz);
}
