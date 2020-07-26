/*
 *  Copyright (C) 2004 Florian Schirmer <jolt@tuxbox.org>
 *  Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 *  Copyright (C) 2006 Michael Buesch <m@bues.ch>
 *  Copyright (C) 2010 Waldemar Brodkorb <wbx@openadk.org>
 *  Copyright (C) 2010-2012 Hauke Mehrtens <hauke@hauke-m.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/bcm47xx_nvram.h>
#include <linux/bcm47xx_sprom.h>
#include <linux/bcma/bcma.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/ssb/ssb.h>

static void create_key(const char *prefix, const char *postfix,
		       const char *name, char *buf, int len)
{
	if (prefix && postfix)
		snprintf(buf, len, "%s%s%s", prefix, name, postfix);
	else if (prefix)
		snprintf(buf, len, "%s%s", prefix, name);
	else if (postfix)
		snprintf(buf, len, "%s%s", name, postfix);
	else
		snprintf(buf, len, "%s", name);
}

static int get_nvram_var(const char *prefix, const char *postfix,
			 const char *name, char *buf, int len, bool fallback)
{
	char key[40];
	int err;

	create_key(prefix, postfix, name, key, sizeof(key));

	err = bcm47xx_nvram_getenv(key, buf, len);
	if (fallback && err == -ENOENT && prefix) {
		create_key(NULL, postfix, name, key, sizeof(key));
		err = bcm47xx_nvram_getenv(key, buf, len);
	}
	return err;
}

#define NVRAM_READ_VAL(type)						\
static void nvram_read_ ## type(const char *prefix,			\
				const char *postfix, const char *name,	\
				type *val, type allset, bool fallback)	\
{									\
	char buf[100];							\
	int err;							\
	type var;							\
									\
	err = get_nvram_var(prefix, postfix, name, buf, sizeof(buf),	\
			    fallback);					\
	if (err < 0)							\
		return;							\
	err = kstrto ## type(strim(buf), 0, &var);			\
	if (err) {							\
		pr_warn("can not parse nvram name %s%s%s with value %s got %i\n",	\
			prefix, name, postfix, buf, err);		\
		return;							\
	}								\
	if (allset && var == allset)					\
		return;							\
	*val = var;							\
}

NVRAM_READ_VAL(u8)
NVRAM_READ_VAL(s8)
NVRAM_READ_VAL(u16)
NVRAM_READ_VAL(u32)

#undef NVRAM_READ_VAL

static void nvram_read_u32_2(const char *prefix, const char *name,
			     u16 *val_lo, u16 *val_hi, bool fallback)
{
	char buf[100];
	int err;
	u32 val;

	err = get_nvram_var(prefix, NULL, name, buf, sizeof(buf), fallback);
	if (err < 0)
		return;
	err = kstrtou32(strim(buf), 0, &val);
	if (err) {
		pr_warn("can not parse nvram name %s%s with value %s got %i\n",
			prefix, name, buf, err);
		return;
	}
	*val_lo = (val & 0x0000FFFFU);
	*val_hi = (val & 0xFFFF0000U) >> 16;
}

static void nvram_read_leddc(const char *prefix, const char *name,
			     u8 *leddc_on_time, u8 *leddc_off_time,
			     bool fallback)
{
	char buf[100];
	int err;
	u32 val;

	err = get_nvram_var(prefix, NULL, name, buf, sizeof(buf), fallback);
	if (err < 0)
		return;
	err = kstrtou32(strim(buf), 0, &val);
	if (err) {
		pr_warn("can not parse nvram name %s%s with value %s got %i\n",
			prefix, name, buf, err);
		return;
	}

	if (val == 0xffff || val == 0xffffffff)
		return;

	*leddc_on_time = val & 0xff;
	*leddc_off_time = (val >> 16) & 0xff;
}

static void nvram_read_macaddr(const char *prefix, const char *name,
			       u8 val[6], bool fallback)
{
	char buf[100];
	int err;

	err = get_nvram_var(prefix, NULL, name, buf, sizeof(buf), fallback);
	if (err < 0)
		return;

	strreplace(buf, '-', ':');
	if (!mac_pton(buf, val))
		pr_warn("Can not parse mac address: %s\n", buf);
}

static void nvram_read_alpha2(const char *prefix, const char *name,
			     char val[2], bool fallback)
{
	char buf[10];
	int err;

	err = get_nvram_var(prefix, NULL, name, buf, sizeof(buf), fallback);
	if (err < 0)
		return;
	if (buf[0] == '0')
		return;
	if (strlen(buf) > 2) {
		pr_warn("alpha2 is too long %s\n", buf);
		return;
	}
	memcpy(val, buf, 2);
}

/* This is one-function-only macro, it uses local "sprom" variable! */
#define ENTRY(_revmask, _type, _prefix, _name, _val, _allset, _fallback) \
	if (_revmask & BIT(sprom->revision)) \
		nvram_read_ ## _type(_prefix, NULL, _name, &sprom->_val, \
				     _allset, _fallback)
/*
 * Special version of filling function that can be safely called for any SPROM
 * revision. For every NVRAM to SPROM mapping it contains bitmask of revisions
 * for which the mapping is valid.
 * It obviously requires some hexadecimal/bitmasks knowledge, but allows
 * writing cleaner code (easy revisions handling).
 * Note that while SPROM revision 0 was never used, we still keep BIT(0)
 * reserved for it, just to keep numbering sane.
 */
static void bcm47xx_sprom_fill_auto(struct ssb_sprom *sprom,
				    const char *prefix, bool fallback)
{
	const char *pre = prefix;
	bool fb = fallback;

	/* Broadcom extracts it for rev 8+ but it was found on 2 and 4 too */
	ENTRY(0xfffffffe, u16, pre, "devid", dev_id, 0, fallback);

	ENTRY(0xfffffffe, u16, pre, "boardrev", board_rev, 0, true);
	ENTRY(0xfffffffe, u32, pre, "boardflags", boardflags, 0, fb);
	ENTRY(0xfffffff0, u32, pre, "boardflags2", boardflags2, 0, fb);
	ENTRY(0xfffff800, u32, pre, "boardflags3", boardflags3, 0, fb);
	ENTRY(0x00000002, u16, pre, "boardflags", boardflags_lo, 0, fb);
	ENTRY(0xfffffffc, u16, pre, "boardtype", board_type, 0, true);
	ENTRY(0xfffffffe, u16, pre, "boardnum", board_num, 0, fb);
	ENTRY(0x00000002, u8, pre, "cc", country_code, 0, fb);
	ENTRY(0xfffffff8, u8, pre, "regrev", regrev, 0, fb);

	ENTRY(0xfffffffe, u8, pre, "ledbh0", gpio0, 0xff, fb);
	ENTRY(0xfffffffe, u8, pre, "ledbh1", gpio1, 0xff, fb);
	ENTRY(0xfffffffe, u8, pre, "ledbh2", gpio2, 0xff, fb);
	ENTRY(0xfffffffe, u8, pre, "ledbh3", gpio3, 0xff, fb);

	ENTRY(0x0000070e, u16, pre, "pa0b0", pa0b0, 0, fb);
	ENTRY(0x0000070e, u16, pre, "pa0b1", pa0b1, 0, fb);
	ENTRY(0x0000070e, u16, pre, "pa0b2", pa0b2, 0, fb);
	ENTRY(0x0000070e, u8, pre, "pa0itssit", itssi_bg, 0, fb);
	ENTRY(0x0000070e, u8, pre, "pa0maxpwr", maxpwr_bg, 0, fb);

	ENTRY(0x0000070c, u8, pre, "opo", opo, 0, fb);
	ENTRY(0xfffffffe, u8, pre, "aa2g", ant_available_bg, 0, fb);
	ENTRY(0xfffffffe, u8, pre, "aa5g", ant_available_a, 0, fb);
	ENTRY(0x000007fe, s8, pre, "ag0", antenna_gain.a0, 0, fb);
	ENTRY(0x000007fe, s8, pre, "ag1", antenna_gain.a1, 0, fb);
	ENTRY(0x000007f0, s8, pre, "ag2", antenna_gain.a2, 0, fb);
	ENTRY(0x000007f0, s8, pre, "ag3", antenna_gain.a3, 0, fb);

	ENTRY(0x0000070e, u16, pre, "pa1b0", pa1b0, 0, fb);
	ENTRY(0x0000070e, u16, pre, "pa1b1", pa1b1, 0, fb);
	ENTRY(0x0000070e, u16, pre, "pa1b2", pa1b2, 0, fb);
	ENTRY(0x0000070c, u16, pre, "pa1lob0", pa1lob0, 0, fb);
	ENTRY(0x0000070c, u16, pre, "pa1lob1", pa1lob1, 0, fb);
	ENTRY(0x0000070c, u16, pre, "pa1lob2", pa1lob2, 0, fb);
	ENTRY(0x0000070c, u16, pre, "pa1hib0", pa1hib0, 0, fb);
	ENTRY(0x0000070c, u16, pre, "pa1hib1", pa1hib1, 0, fb);
	ENTRY(0x0000070c, u16, pre, "pa1hib2", pa1hib2, 0, fb);
	ENTRY(0x0000070e, u8, pre, "pa1itssit", itssi_a, 0, fb);
	ENTRY(0x0000070e, u8, pre, "pa1maxpwr", maxpwr_a, 0, fb);
	ENTRY(0x0000070c, u8, pre, "pa1lomaxpwr", maxpwr_al, 0, fb);
	ENTRY(0x0000070c, u8, pre, "pa1himaxpwr", maxpwr_ah, 0, fb);

	ENTRY(0x00000708, u8, pre, "bxa2g", bxa2g, 0, fb);
	ENTRY(0x00000708, u8, pre, "rssisav2g", rssisav2g, 0, fb);
	ENTRY(0x00000708, u8, pre, "rssismc2g", rssismc2g, 0, fb);
	ENTRY(0x00000708, u8, pre, "rssismf2g", rssismf2g, 0, fb);
	ENTRY(0x00000708, u8, pre, "bxa5g", bxa5g, 0, fb);
	ENTRY(0x00000708, u8, pre, "rssisav5g", rssisav5g, 0, fb);
	ENTRY(0x00000708, u8, pre, "rssismc5g", rssismc5g, 0, fb);
	ENTRY(0x00000708, u8, pre, "rssismf5g", rssismf5g, 0, fb);
	ENTRY(0x00000708, u8, pre, "tri2g", tri2g, 0, fb);
	ENTRY(0x00000708, u8, pre, "tri5g", tri5g, 0, fb);
	ENTRY(0x00000708, u8, pre, "tri5gl", tri5gl, 0, fb);
	ENTRY(0x00000708, u8, pre, "tri5gh", tri5gh, 0, fb);
	ENTRY(0x00000708, s8, pre, "rxpo2g", rxpo2g, 0, fb);
	ENTRY(0x00000708, s8, pre, "rxpo5g", rxpo5g, 0, fb);
	ENTRY(0xfffffff0, u8, pre, "txchain", txchain, 0xf, fb);
	ENTRY(0xfffffff0, u8, pre, "rxchain", rxchain, 0xf, fb);
	ENTRY(0xfffffff0, u8, pre, "antswitch", antswitch, 0xff, fb);
	ENTRY(0x00000700, u8, pre, "tssipos2g", fem.ghz2.tssipos, 0, fb);
	ENTRY(0x00000700, u8, pre, "extpagain2g", fem.ghz2.extpa_gain, 0, fb);
	ENTRY(0x00000700, u8, pre, "pdetrange2g", fem.ghz2.pdet_range, 0, fb);
	ENTRY(0x00000700, u8, pre, "triso2g", fem.ghz2.tr_iso, 0, fb);
	ENTRY(0x00000700, u8, pre, "antswctl2g", fem.ghz2.antswlut, 0, fb);
	ENTRY(0x00000700, u8, pre, "tssipos5g", fem.ghz5.tssipos, 0, fb);
	ENTRY(0x00000700, u8, pre, "extpagain5g", fem.ghz5.extpa_gain, 0, fb);
	ENTRY(0x00000700, u8, pre, "pdetrange5g", fem.ghz5.pdet_range, 0, fb);
	ENTRY(0x00000700, u8, pre, "triso5g", fem.ghz5.tr_iso, 0, fb);
	ENTRY(0x00000700, u8, pre, "antswctl5g", fem.ghz5.antswlut, 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid2ga0", txpid2g[0], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid2ga1", txpid2g[1], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid2ga2", txpid2g[2], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid2ga3", txpid2g[3], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5ga0", txpid5g[0], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5ga1", txpid5g[1], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5ga2", txpid5g[2], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5ga3", txpid5g[3], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gla0", txpid5gl[0], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gla1", txpid5gl[1], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gla2", txpid5gl[2], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gla3", txpid5gl[3], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gha0", txpid5gh[0], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gha1", txpid5gh[1], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gha2", txpid5gh[2], 0, fb);
	ENTRY(0x000000f0, u8, pre, "txpid5gha3", txpid5gh[3], 0, fb);

	ENTRY(0xffffff00, u8, pre, "tempthresh", tempthresh, 0, fb);
	ENTRY(0xffffff00, u8, pre, "tempoffset", tempoffset, 0, fb);
	ENTRY(0xffffff00, u16, pre, "rawtempsense", rawtempsense, 0, fb);
	ENTRY(0xffffff00, u8, pre, "measpower", measpower, 0, fb);
	ENTRY(0xffffff00, u8, pre, "tempsense_slope", tempsense_slope, 0, fb);
	ENTRY(0xffffff00, u8, pre, "tempcorrx", tempcorrx, 0, fb);
	ENTRY(0xffffff00, u8, pre, "tempsense_option", tempsense_option, 0, fb);
	ENTRY(0x00000700, u8, pre, "freqoffset_corr", freqoffset_corr, 0, fb);
	ENTRY(0x00000700, u8, pre, "iqcal_swp_dis", iqcal_swp_dis, 0, fb);
	ENTRY(0x00000700, u8, pre, "hw_iqcal_en", hw_iqcal_en, 0, fb);
	ENTRY(0x00000700, u8, pre, "elna2g", elna2g, 0, fb);
	ENTRY(0x00000700, u8, pre, "elna5g", elna5g, 0, fb);
	ENTRY(0xffffff00, u8, pre, "phycal_tempdelta", phycal_tempdelta, 0, fb);
	ENTRY(0xffffff00, u8, pre, "temps_period", temps_period, 0, fb);
	ENTRY(0xffffff00, u8, pre, "temps_hysteresis", temps_hysteresis, 0, fb);
	ENTRY(0xffffff00, u8, pre, "measpower1", measpower1, 0, fb);
	ENTRY(0xffffff00, u8, pre, "measpower2", measpower2, 0, fb);

	ENTRY(0x000001f0, u16, pre, "cck2gpo", cck2gpo, 0, fb);
	ENTRY(0x000001f0, u32, pre, "ofdm2gpo", ofdm2gpo, 0, fb);
	ENTRY(0x000001f0, u32, pre, "ofdm5gpo", ofdm5gpo, 0, fb);
	ENTRY(0x000001f0, u32, pre, "ofdm5glpo", ofdm5glpo, 0, fb);
	ENTRY(0x000001f0, u32, pre, "ofdm5ghpo", ofdm5ghpo, 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo0", mcs2gpo[0], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo1", mcs2gpo[1], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo2", mcs2gpo[2], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo3", mcs2gpo[3], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo4", mcs2gpo[4], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo5", mcs2gpo[5], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo6", mcs2gpo[6], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs2gpo7", mcs2gpo[7], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo0", mcs5gpo[0], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo1", mcs5gpo[1], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo2", mcs5gpo[2], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo3", mcs5gpo[3], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo4", mcs5gpo[4], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo5", mcs5gpo[5], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo6", mcs5gpo[6], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5gpo7", mcs5gpo[7], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo0", mcs5glpo[0], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo1", mcs5glpo[1], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo2", mcs5glpo[2], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo3", mcs5glpo[3], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo4", mcs5glpo[4], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo5", mcs5glpo[5], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo6", mcs5glpo[6], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5glpo7", mcs5glpo[7], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo0", mcs5ghpo[0], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo1", mcs5ghpo[1], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo2", mcs5ghpo[2], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo3", mcs5ghpo[3], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo4", mcs5ghpo[4], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo5", mcs5ghpo[5], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo6", mcs5ghpo[6], 0, fb);
	ENTRY(0x000001f0, u16, pre, "mcs5ghpo7", mcs5ghpo[7], 0, fb);
	ENTRY(0x000001f0, u16, pre, "cddpo", cddpo, 0, fb);
	ENTRY(0x000001f0, u16, pre, "stbcpo", stbcpo, 0, fb);
	ENTRY(0x000001f0, u16, pre, "bw40po", bw40po, 0, fb);
	ENTRY(0x000001f0, u16, pre, "bwduppo", bwduppo, 0, fb);

	ENTRY(0xfffffe00, u16, pre, "cckbw202gpo", cckbw202gpo, 0, fb);
	ENTRY(0xfffffe00, u16, pre, "cckbw20ul2gpo", cckbw20ul2gpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw202gpo", legofdmbw202gpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw20ul2gpo", legofdmbw20ul2gpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw205glpo", legofdmbw205glpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw20ul5glpo", legofdmbw20ul5glpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw205gmpo", legofdmbw205gmpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw20ul5gmpo", legofdmbw20ul5gmpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw205ghpo", legofdmbw205ghpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "legofdmbw20ul5ghpo", legofdmbw20ul5ghpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw202gpo", mcsbw202gpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "mcsbw20ul2gpo", mcsbw20ul2gpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw402gpo", mcsbw402gpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw205glpo", mcsbw205glpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "mcsbw20ul5glpo", mcsbw20ul5glpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw405glpo", mcsbw405glpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw205gmpo", mcsbw205gmpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "mcsbw20ul5gmpo", mcsbw20ul5gmpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw405gmpo", mcsbw405gmpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw205ghpo", mcsbw205ghpo, 0, fb);
	ENTRY(0x00000600, u32, pre, "mcsbw20ul5ghpo", mcsbw20ul5ghpo, 0, fb);
	ENTRY(0xfffffe00, u32, pre, "mcsbw405ghpo", mcsbw405ghpo, 0, fb);
	ENTRY(0x00000600, u16, pre, "mcs32po", mcs32po, 0, fb);
	ENTRY(0x00000600, u16, pre, "legofdm40duppo", legofdm40duppo, 0, fb);
	ENTRY(0x00000700, u8, pre, "pcieingress_war", pcieingress_war, 0, fb);

	/* TODO: rev 11 support */
	ENTRY(0x00000700, u8, pre, "rxgainerr2ga0", rxgainerr2ga[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr2ga1", rxgainerr2ga[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr2ga2", rxgainerr2ga[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gla0", rxgainerr5gla[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gla1", rxgainerr5gla[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gla2", rxgainerr5gla[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gma0", rxgainerr5gma[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gma1", rxgainerr5gma[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gma2", rxgainerr5gma[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gha0", rxgainerr5gha[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gha1", rxgainerr5gha[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gha2", rxgainerr5gha[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gua0", rxgainerr5gua[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gua1", rxgainerr5gua[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "rxgainerr5gua2", rxgainerr5gua[2], 0, fb);

	ENTRY(0xfffffe00, u8, pre, "sar2g", sar2g, 0, fb);
	ENTRY(0xfffffe00, u8, pre, "sar5g", sar5g, 0, fb);

	/* TODO: rev 11 support */
	ENTRY(0x00000700, u8, pre, "noiselvl2ga0", noiselvl2ga[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl2ga1", noiselvl2ga[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl2ga2", noiselvl2ga[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gla0", noiselvl5gla[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gla1", noiselvl5gla[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gla2", noiselvl5gla[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gma0", noiselvl5gma[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gma1", noiselvl5gma[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gma2", noiselvl5gma[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gha0", noiselvl5gha[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gha1", noiselvl5gha[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gha2", noiselvl5gha[2], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gua0", noiselvl5gua[0], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gua1", noiselvl5gua[1], 0, fb);
	ENTRY(0x00000700, u8, pre, "noiselvl5gua2", noiselvl5gua[2], 0, fb);
}
#undef ENTRY /* It's specififc, uses local variable, don't use it (again). */

static void bcm47xx_fill_sprom_path_r4589(struct ssb_sprom *sprom,
					  const char *prefix, bool fallback)
{
	char postfix[2];
	int i;

	for (i = 0; i < ARRAY_SIZE(sprom->core_pwr_info); i++) {
		struct ssb_sprom_core_pwr_info *pwr_info;

		pwr_info = &sprom->core_pwr_info[i];

		snprintf(postfix, sizeof(postfix), "%i", i);
		nvram_read_u8(prefix, postfix, "maxp2ga",
			      &pwr_info->maxpwr_2g, 0, fallback);
		nvram_read_u8(prefix, postfix, "itt2ga",
			      &pwr_info->itssi_2g, 0, fallback);
		nvram_read_u8(prefix, postfix, "itt5ga",
			      &pwr_info->itssi_5g, 0, fallback);
		nvram_read_u16(prefix, postfix, "pa2gw0a",
			       &pwr_info->pa_2g[0], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa2gw1a",
			       &pwr_info->pa_2g[1], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa2gw2a",
			       &pwr_info->pa_2g[2], 0, fallback);
		nvram_read_u8(prefix, postfix, "maxp5ga",
			      &pwr_info->maxpwr_5g, 0, fallback);
		nvram_read_u8(prefix, postfix, "maxp5gha",
			      &pwr_info->maxpwr_5gh, 0, fallback);
		nvram_read_u8(prefix, postfix, "maxp5gla",
			      &pwr_info->maxpwr_5gl, 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5gw0a",
			       &pwr_info->pa_5g[0], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5gw1a",
			       &pwr_info->pa_5g[1], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5gw2a",
			       &pwr_info->pa_5g[2], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5glw0a",
			       &pwr_info->pa_5gl[0], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5glw1a",
			       &pwr_info->pa_5gl[1], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5glw2a",
			       &pwr_info->pa_5gl[2], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5ghw0a",
			       &pwr_info->pa_5gh[0], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5ghw1a",
			       &pwr_info->pa_5gh[1], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5ghw2a",
			       &pwr_info->pa_5gh[2], 0, fallback);
	}
}

static void bcm47xx_fill_sprom_path_r45(struct ssb_sprom *sprom,
					const char *prefix, bool fallback)
{
	char postfix[2];
	int i;

	for (i = 0; i < ARRAY_SIZE(sprom->core_pwr_info); i++) {
		struct ssb_sprom_core_pwr_info *pwr_info;

		pwr_info = &sprom->core_pwr_info[i];

		snprintf(postfix, sizeof(postfix), "%i", i);
		nvram_read_u16(prefix, postfix, "pa2gw3a",
			       &pwr_info->pa_2g[3], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5gw3a",
			       &pwr_info->pa_5g[3], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5glw3a",
			       &pwr_info->pa_5gl[3], 0, fallback);
		nvram_read_u16(prefix, postfix, "pa5ghw3a",
			       &pwr_info->pa_5gh[3], 0, fallback);
	}
}

static bool bcm47xx_is_valid_mac(u8 *mac)
{
	return mac && !(mac[0] == 0x00 && mac[1] == 0x90 && mac[2] == 0x4c);
}

static int bcm47xx_increase_mac_addr(u8 *mac, u8 num)
{
	u8 *oui = mac + ETH_ALEN/2 - 1;
	u8 *p = mac + ETH_ALEN - 1;

	do {
		(*p) += num;
		if (*p > num)
			break;
		p--;
		num = 1;
	} while (p != oui);

	if (p == oui) {
		pr_err("unable to fetch mac address\n");
		return -ENOENT;
	}
	return 0;
}

static int mac_addr_used = 2;

static void bcm47xx_fill_sprom_ethernet(struct ssb_sprom *sprom,
					const char *prefix, bool fallback)
{
	bool fb = fallback;

	nvram_read_macaddr(prefix, "et0macaddr", sprom->et0mac, fallback);
	nvram_read_u8(prefix, NULL, "et0mdcport", &sprom->et0mdcport, 0,
		      fallback);
	nvram_read_u8(prefix, NULL, "et0phyaddr", &sprom->et0phyaddr, 0,
		      fallback);

	nvram_read_macaddr(prefix, "et1macaddr", sprom->et1mac, fallback);
	nvram_read_u8(prefix, NULL, "et1mdcport", &sprom->et1mdcport, 0,
		      fallback);
	nvram_read_u8(prefix, NULL, "et1phyaddr", &sprom->et1phyaddr, 0,
		      fallback);

	nvram_read_macaddr(prefix, "et2macaddr", sprom->et2mac, fb);
	nvram_read_u8(prefix, NULL, "et2mdcport", &sprom->et2mdcport, 0, fb);
	nvram_read_u8(prefix, NULL, "et2phyaddr", &sprom->et2phyaddr, 0, fb);

	nvram_read_macaddr(prefix, "macaddr", sprom->il0mac, fallback);
	nvram_read_macaddr(prefix, "il0macaddr", sprom->il0mac, fallback);

	/* The address prefix 00:90:4C is used by Broadcom in their initial
	 * configuration. When a mac address with the prefix 00:90:4C is used
	 * all devices from the same series are sharing the same mac address.
	 * To prevent mac address collisions we replace them with a mac address
	 * based on the base address.
	 */
	if (!bcm47xx_is_valid_mac(sprom->il0mac)) {
		u8 mac[6];

		nvram_read_macaddr(NULL, "et0macaddr", mac, false);
		if (bcm47xx_is_valid_mac(mac)) {
			int err = bcm47xx_increase_mac_addr(mac, mac_addr_used);

			if (!err) {
				ether_addr_copy(sprom->il0mac, mac);
				mac_addr_used++;
			}
		}
	}
}

static void bcm47xx_fill_board_data(struct ssb_sprom *sprom, const char *prefix,
				    bool fallback)
{
	nvram_read_u32_2(prefix, "boardflags", &sprom->boardflags_lo,
			 &sprom->boardflags_hi, fallback);
	nvram_read_u32_2(prefix, "boardflags2", &sprom->boardflags2_lo,
			 &sprom->boardflags2_hi, fallback);
}

void bcm47xx_fill_sprom(struct ssb_sprom *sprom, const char *prefix,
			bool fallback)
{
	bcm47xx_fill_sprom_ethernet(sprom, prefix, fallback);
	bcm47xx_fill_board_data(sprom, prefix, fallback);

	nvram_read_u8(prefix, NULL, "sromrev", &sprom->revision, 0, fallback);

	/* Entries requiring custom functions */
	nvram_read_alpha2(prefix, "ccode", sprom->alpha2, fallback);
	if (sprom->revision >= 3)
		nvram_read_leddc(prefix, "leddc", &sprom->leddc_on_time,
				 &sprom->leddc_off_time, fallback);

	switch (sprom->revision) {
	case 4:
	case 5:
		bcm47xx_fill_sprom_path_r4589(sprom, prefix, fallback);
		bcm47xx_fill_sprom_path_r45(sprom, prefix, fallback);
		break;
	case 8:
	case 9:
		bcm47xx_fill_sprom_path_r4589(sprom, prefix, fallback);
		break;
	}

	bcm47xx_sprom_fill_auto(sprom, prefix, fallback);
}

#if IS_BUILTIN(CONFIG_SSB) && IS_ENABLED(CONFIG_SSB_SPROM)
static int bcm47xx_get_sprom_ssb(struct ssb_bus *bus, struct ssb_sprom *out)
{
	char prefix[10];

	switch (bus->bustype) {
	case SSB_BUSTYPE_SSB:
		bcm47xx_fill_sprom(out, NULL, false);
		return 0;
	case SSB_BUSTYPE_PCI:
		memset(out, 0, sizeof(struct ssb_sprom));
		snprintf(prefix, sizeof(prefix), "pci/%u/%u/",
			 bus->host_pci->bus->number + 1,
			 PCI_SLOT(bus->host_pci->devfn));
		bcm47xx_fill_sprom(out, prefix, false);
		return 0;
	default:
		pr_warn("Unable to fill SPROM for given bustype.\n");
		return -EINVAL;
	}
}
#endif

#if IS_BUILTIN(CONFIG_BCMA)
/*
 * Having many NVRAM entries for PCI devices led to repeating prefixes like
 * pci/1/1/ all the time and wasting flash space. So at some point Broadcom
 * decided to introduce prefixes like 0: 1: 2: etc.
 * If we find e.g. devpath0=pci/2/1 or devpath0=pci/2/1/ we should use 0:
 * instead of pci/2/1/.
 */
static void bcm47xx_sprom_apply_prefix_alias(char *prefix, size_t prefix_size)
{
	size_t prefix_len = strlen(prefix);
	size_t short_len = prefix_len - 1;
	char nvram_var[10];
	char buf[20];
	int i;

	/* Passed prefix has to end with a slash */
	if (prefix_len <= 0 || prefix[prefix_len - 1] != '/')
		return;

	for (i = 0; i < 3; i++) {
		if (snprintf(nvram_var, sizeof(nvram_var), "devpath%d", i) <= 0)
			continue;
		if (bcm47xx_nvram_getenv(nvram_var, buf, sizeof(buf)) < 0)
			continue;
		if (!strcmp(buf, prefix) ||
		    (short_len && strlen(buf) == short_len && !strncmp(buf, prefix, short_len))) {
			snprintf(prefix, prefix_size, "%d:", i);
			return;
		}
	}
}

static int bcm47xx_get_sprom_bcma(struct bcma_bus *bus, struct ssb_sprom *out)
{
	struct bcma_boardinfo *binfo = &bus->boardinfo;
	struct bcma_device *core;
	char buf[10];
	char *prefix;
	bool fallback = false;

	switch (bus->hosttype) {
	case BCMA_HOSTTYPE_PCI:
		memset(out, 0, sizeof(struct ssb_sprom));
		/* On BCM47XX all PCI buses share the same domain */
		if (IS_ENABLED(CONFIG_BCM47XX))
			snprintf(buf, sizeof(buf), "pci/%u/%u/",
				 bus->host_pci->bus->number + 1,
				 PCI_SLOT(bus->host_pci->devfn));
		else
			snprintf(buf, sizeof(buf), "pci/%u/%u/",
				 pci_domain_nr(bus->host_pci->bus) + 1,
				 bus->host_pci->bus->number);
		bcm47xx_sprom_apply_prefix_alias(buf, sizeof(buf));
		prefix = buf;
		break;
	case BCMA_HOSTTYPE_SOC:
		memset(out, 0, sizeof(struct ssb_sprom));
		core = bcma_find_core(bus, BCMA_CORE_80211);
		if (core) {
			snprintf(buf, sizeof(buf), "sb/%u/",
				 core->core_index);
			prefix = buf;
			fallback = true;
		} else {
			prefix = NULL;
		}
		break;
	default:
		pr_warn("Unable to fill SPROM for given bustype.\n");
		return -EINVAL;
	}

	nvram_read_u16(prefix, NULL, "boardvendor", &binfo->vendor, 0, true);
	if (!binfo->vendor)
		binfo->vendor = SSB_BOARDVENDOR_BCM;
	nvram_read_u16(prefix, NULL, "boardtype", &binfo->type, 0, true);

	bcm47xx_fill_sprom(out, prefix, fallback);

	return 0;
}
#endif

static unsigned int bcm47xx_sprom_registered;

/*
 * On bcm47xx we need to register SPROM fallback handler very early, so we can't
 * use anything like platform device / driver for this.
 */
int bcm47xx_sprom_register_fallbacks(void)
{
	if (bcm47xx_sprom_registered)
		return 0;

#if IS_BUILTIN(CONFIG_SSB) && IS_ENABLED(CONFIG_SSB_SPROM)
	if (ssb_arch_register_fallback_sprom(&bcm47xx_get_sprom_ssb))
		pr_warn("Failed to register ssb SPROM handler\n");
#endif

#if IS_BUILTIN(CONFIG_BCMA)
	if (bcma_arch_register_fallback_sprom(&bcm47xx_get_sprom_bcma))
		pr_warn("Failed to register bcma SPROM handler\n");
#endif

	bcm47xx_sprom_registered = 1;

	return 0;
}

fs_initcall(bcm47xx_sprom_register_fallbacks);
