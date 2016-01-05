/*
 * Misc utility routines used by kernel or app-level.
 * Contents are wifi-specific, used by any kernel or app-level
 * software that might want wifi things as it grows.
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
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
 * $Id: bcmwifi_channels.c 309193 2012-01-19 00:03:57Z $
 */

#include <bcm_cfg.h>
#include <typedefs.h>
#include <bcmutils.h>

#ifdef BCMDRIVER
#include <osl.h>
#define strtoul(nptr, endptr, base) bcm_strtoul((nptr), (endptr), (base))
#define tolower(c) (bcm_isupper((c)) ? ((c) + 'a' - 'A') : (c))
#else
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#ifndef ASSERT
#define ASSERT(exp)
#endif
#endif /* BCMDRIVER */

#include <bcmwifi_channels.h>

#if defined(WIN32) && (defined(BCMDLL) || defined(WLMDLL))
#include <bcmstdlib.h> 	/* For wl/exe/GNUmakefile.brcm_wlu and GNUmakefile.wlm_dll */
#endif

/* Definitions for D11AC capable Chanspec type */

/* Chanspec ASCII representation with 802.11ac capability:
 * [<band> 'g'] <channel> ['/'<bandwidth> [<ctl-sideband>]['/'<1st80channel>'-'<2nd80channel>]]
 *
 * <band>:
 *      (optional) 2, 3, 4, 5 for 2.4GHz, 3GHz, 4GHz, and 5GHz respectively.
 *      Default value is 2g if channel <= 14, otherwise 5g.
 * <channel>:
 *      channel number of the 5MHz, 10MHz, 20MHz channel,
 *      or primary channel of 40MHz, 80MHz, 160MHz, or 80+80MHz channel.
 * <bandwidth>:
 *      (optional) 5, 10, 20, 40, 80, 160, or 80+80. Default value is 20.
 * <primary-sideband>:
 *      (only for 2.4GHz band 40MHz) U for upper sideband primary, L for lower.
 *
 *      For 2.4GHz band 40MHz channels, the same primary channel may be the
 *      upper sideband for one 40MHz channel, and the lower sideband for an
 *      overlapping 40MHz channel.  The U/L disambiguates which 40MHz channel
 *      is being specified.
 *
 *      For 40MHz in the 5GHz band and all channel bandwidths greater than
 *      40MHz, the U/L specificaion is not allowed since the channels are
 *      non-overlapping and the primary sub-band is derived from its
 *      position in the wide bandwidth channel.
 *
 * <1st80Channel>:
 * <2nd80Channel>:
 *      Required for 80+80, otherwise not allowed.
 *      Specifies the center channel of the first and second 80MHz band.
 *
 * In its simplest form, it is a 20MHz channel number, with the implied band
 * of 2.4GHz if channel number <= 14, and 5GHz otherwise.
 *
 * To allow for backward compatibility with scripts, the old form for
 * 40MHz channels is also allowed: <channel><ctl-sideband>
 *
 * <channel>:
 *	primary channel of 40MHz, channel <= 14 is 2GHz, otherwise 5GHz
 * <ctl-sideband>:
 * 	"U" for upper, "L" for lower (or lower case "u" "l")
 *
 * 5 GHz Examples:
 *      Chanspec        BW        Center Ch  Channel Range  Primary Ch
 *      5g8             20MHz     8          -              -
 *      52              20MHz     52         -              -
 *      52/40           40MHz     54         52-56          52
 *      56/40           40MHz     54         52-56          56
 *      52/80           80MHz     58         52-64          52
 *      56/80           80MHz     58         52-64          56
 *      60/80           80MHz     58         52-64          60
 *      64/80           80MHz     58         52-64          64
 *      52/160          160MHz    50         36-64          52
 *      36/160          160MGz    50         36-64          36
 *      36/80+80/42-106 80+80MHz  42,106     36-48,100-112  36
 *
 * 2 GHz Examples:
 *      Chanspec        BW        Center Ch  Channel Range  Primary Ch
 *      2g8             20MHz     8          -              -
 *      8               20MHz     8          -              -
 *      6               20MHz     6          -              -
 *      6/40l           40MHz     8          6-10           6
 *      6l              40MHz     8          6-10           6
 *      6/40u           40MHz     4          2-6            6
 *      6u              40MHz     4          2-6            6
 */

/* bandwidth ASCII string */
static const char *wf_chspec_bw_str[] =
{
	"5",
	"10",
	"20",
	"40",
	"80",
	"160",
	"80+80",
	"na"
};

static const uint8 wf_chspec_bw_mhz[] =
{5, 10, 20, 40, 80, 160, 160};

#define WF_NUM_BW \
	(sizeof(wf_chspec_bw_mhz)/sizeof(uint8))

/* 40MHz channels in 5GHz band */
static const uint8 wf_5g_40m_chans[] =
{38, 46, 54, 62, 102, 110, 118, 126, 134, 142, 151, 159};
#define WF_NUM_5G_40M_CHANS \
	(sizeof(wf_5g_40m_chans)/sizeof(uint8))

/* 80MHz channels in 5GHz band */
static const uint8 wf_5g_80m_chans[] =
{42, 58, 106, 122, 138, 155};
#define WF_NUM_5G_80M_CHANS \
	(sizeof(wf_5g_80m_chans)/sizeof(uint8))

/* 160MHz channels in 5GHz band */
static const uint8 wf_5g_160m_chans[] =
{50, 114};
#define WF_NUM_5G_160M_CHANS \
	(sizeof(wf_5g_160m_chans)/sizeof(uint8))


/* convert bandwidth from chanspec to MHz */
static uint
bw_chspec_to_mhz(chanspec_t chspec)
{
	uint bw;

	bw = (chspec & WL_CHANSPEC_BW_MASK) >> WL_CHANSPEC_BW_SHIFT;
	return (bw >= WF_NUM_BW ? 0 : wf_chspec_bw_mhz[bw]);
}

/* bw in MHz, return the channel count from the center channel to the
 * the channel at the edge of the band
 */
static uint8
center_chan_to_edge(uint bw)
{
	/* edge channels separated by BW - 10MHz on each side
	 * delta from cf to edge is half of that,
	 * MHz to channel num conversion is 5MHz/channel
	 */
	return (uint8)(((bw - 20) / 2) / 5);
}

/* return channel number of the low edge of the band
 * given the center channel and BW
 */
static uint8
channel_low_edge(uint center_ch, uint bw)
{
	return (uint8)(center_ch - center_chan_to_edge(bw));
}

/* return side band number given center channel and control channel
 * return -1 on error
 */
static int
channel_to_sb(uint center_ch, uint ctl_ch, uint bw)
{
	uint lowest = channel_low_edge(center_ch, bw);
	uint sb;

	if ((ctl_ch - lowest) % 4) {
		/* bad ctl channel, not mult 4 */
		return -1;
	}

	sb = ((ctl_ch - lowest) / 4);

	/* sb must be a index to a 20MHz channel in range */
	if (sb >= (bw / 20)) {
		/* ctl_ch must have been too high for the center_ch */
		return -1;
	}

	return sb;
}

/* return control channel given center channel and side band */
static uint8
channel_to_ctl_chan(uint center_ch, uint bw, uint sb)
{
	return (uint8)(channel_low_edge(center_ch, bw) + sb * 4);
}

/* return index of 80MHz channel from channel number
 * return -1 on error
 */
static int
channel_80mhz_to_id(uint ch)
{
	uint i;
	for (i = 0; i < WF_NUM_5G_80M_CHANS; i ++) {
		if (ch == wf_5g_80m_chans[i])
			return i;
	}

	return -1;
}

/* given a chanspec and a string buffer, format the chanspec as a
 * string, and return the original pointer a.
 * Min buffer length must be CHANSPEC_STR_LEN.
 * On error return NULL
 */
char *
wf_chspec_ntoa(chanspec_t chspec, char *buf)
{
	const char *band;
	uint ctl_chan;

	if (wf_chspec_malformed(chspec))
		return NULL;

	band = "";

	/* check for non-default band spec */
	if ((CHSPEC_IS2G(chspec) && CHSPEC_CHANNEL(chspec) > CH_MAX_2G_CHANNEL) ||
	    (CHSPEC_IS5G(chspec) && CHSPEC_CHANNEL(chspec) <= CH_MAX_2G_CHANNEL))
		band = (CHSPEC_IS2G(chspec)) ? "2g" : "5g";

	/* ctl channel */
	ctl_chan = wf_chspec_ctlchan(chspec);

	/* bandwidth and ctl sideband */
	if (CHSPEC_IS20(chspec)) {
		snprintf(buf, CHANSPEC_STR_LEN, "%s%d", band, ctl_chan);
	} else if (!CHSPEC_IS8080(chspec)) {
		const char *bw;
		const char *sb = "";

		bw = wf_chspec_bw_str[(chspec & WL_CHANSPEC_BW_MASK) >> WL_CHANSPEC_BW_SHIFT];

#ifdef CHANSPEC_NEW_40MHZ_FORMAT
		/* ctl sideband string if needed for 2g 40MHz */
		if (CHSPEC_IS40(chspec) && CHSPEC_IS2G(chspec)) {
			sb = CHSPEC_SB_UPPER(chspec) ? "u" : "l";
		}

		snprintf(buf, CHANSPEC_STR_LEN, "%s%d/%s%s", band, ctl_chan, bw, sb);
#else
		/* ctl sideband string instead of BW for 40MHz */
		if (CHSPEC_IS40(chspec)) {
			sb = CHSPEC_SB_UPPER(chspec) ? "u" : "l";
			snprintf(buf, CHANSPEC_STR_LEN, "%s%d%s", band, ctl_chan, sb);
		} else {
			snprintf(buf, CHANSPEC_STR_LEN, "%s%d/%s", band, ctl_chan, bw);
		}
#endif /* CHANSPEC_NEW_40MHZ_FORMAT */

	} else {
		/* 80+80 */
		uint chan1 = (chspec & WL_CHANSPEC_CHAN1_MASK) >> WL_CHANSPEC_CHAN1_SHIFT;
		uint chan2 = (chspec & WL_CHANSPEC_CHAN2_MASK) >> WL_CHANSPEC_CHAN2_SHIFT;

		/* convert to channel number */
		chan1 = (chan1 < WF_NUM_5G_80M_CHANS) ? wf_5g_80m_chans[chan1] : 0;
		chan2 = (chan2 < WF_NUM_5G_80M_CHANS) ? wf_5g_80m_chans[chan2] : 0;

		/* Outputs a max of CHANSPEC_STR_LEN chars including '\0'  */
		snprintf(buf, CHANSPEC_STR_LEN, "%d/80+80/%d-%d", ctl_chan, chan1, chan2);
	}

	return (buf);
}

static int
read_uint(const char **p, unsigned int *num)
{
	unsigned long val;
	char *endp = NULL;

	val = strtoul(*p, &endp, 10);
	/* if endp is the initial pointer value, then a number was not read */
	if (endp == *p)
		return 0;

	/* advance the buffer pointer to the end of the integer string */
	*p = endp;
	/* return the parsed integer */
	*num = (unsigned int)val;

	return 1;
}

/* given a chanspec string, convert to a chanspec.
 * On error return 0
 */
chanspec_t
wf_chspec_aton(const char *a)
{
	chanspec_t chspec;
	uint chspec_ch, chspec_band, bw, chspec_bw, chspec_sb;
	uint num, ctl_ch;
	uint ch1, ch2;
	char c, sb_ul = '\0';
	int i;

	bw = 20;
	chspec_sb = 0;
	chspec_ch = ch1 = ch2 = 0;

	/* parse channel num or band */
	if (!read_uint(&a, &num))
		return 0;

	/* if we are looking at a 'g', then the first number was a band */
	c = tolower((int)a[0]);
	if (c == 'g') {
		a ++; /* consume the char */

		/* band must be "2" or "5" */
		if (num == 2)
			chspec_band = WL_CHANSPEC_BAND_2G;
		else if (num == 5)
			chspec_band = WL_CHANSPEC_BAND_5G;
		else
			return 0;

		/* read the channel number */
		if (!read_uint(&a, &ctl_ch))
			return 0;

		c = tolower((int)a[0]);
	}
	else {
		/* first number is channel, use default for band */
		ctl_ch = num;
		chspec_band = ((ctl_ch <= CH_MAX_2G_CHANNEL) ?
		               WL_CHANSPEC_BAND_2G : WL_CHANSPEC_BAND_5G);
	}

	if (c == '\0') {
		/* default BW of 20MHz */
		chspec_bw = WL_CHANSPEC_BW_20;
		goto done_read;
	}

	a ++; /* consume the 'u','l', or '/' */

	/* check 'u'/'l' */
	if (c == 'u' || c == 'l') {
		sb_ul = c;
		chspec_bw = WL_CHANSPEC_BW_40;
		goto done_read;
	}

	/* next letter must be '/' */
	if (c != '/')
		return 0;

	/* read bandwidth */
	if (!read_uint(&a, &bw))
		return 0;

	/* convert to chspec value */
	if (bw == 20) {
		chspec_bw = WL_CHANSPEC_BW_20;
	} else if (bw == 40) {
		chspec_bw = WL_CHANSPEC_BW_40;
	} else if (bw == 80) {
		chspec_bw = WL_CHANSPEC_BW_80;
	} else if (bw == 160) {
		chspec_bw = WL_CHANSPEC_BW_160;
	} else {
		return 0;
	}

	/* So far we have <band>g<chan>/<bw>
	 * Can now be followed by u/l if bw = 40,
	 * or '+80' if bw = 80, to make '80+80' bw.
	 */

	c = tolower((int)a[0]);

	/* if we have a 2g/40 channel, we should have a l/u spec now */
	if (chspec_band == WL_CHANSPEC_BAND_2G && bw == 40) {
		if (c == 'u' || c == 'l') {
			a ++; /* consume the u/l char */
			sb_ul = c;
			goto done_read;
		}
	}

	/* check for 80+80 */
	if (c == '+') {
		/* 80+80 */
		static const char *plus80 = "80/";

		/* must be looking at '+80/'
		 * check and consume this string.
		 */
		chspec_bw = WL_CHANSPEC_BW_8080;

		a ++; /* consume the char '+' */

		/* consume the '80/' string */
		for (i = 0; i < 3; i++) {
			if (*a++ != *plus80++) {
				return 0;
			}
		}

		/* read primary 80MHz channel */
		if (!read_uint(&a, &ch1))
			return 0;

		/* must followed by '-' */
		if (a[0] != '-')
			return 0;
		a ++; /* consume the char */

		/* read secondary 80MHz channel */
		if (!read_uint(&a, &ch2))
			return 0;
	}

done_read:
	/* skip trailing white space */
	while (a[0] == ' ') {
		a ++;
	}

	/* must be end of string */
	if (a[0] != '\0')
		return 0;

	/* Now have all the chanspec string parts read;
	 * chspec_band, ctl_ch, chspec_bw, sb_ul, ch1, ch2.
	 * chspec_band and chspec_bw are chanspec values.
	 * Need to convert ctl_ch, sb_ul, and ch1,ch2 into
	 * a center channel (or two) and sideband.
	 */

	/* if a sb u/l string was given, just use that,
	 * guaranteed to be bw = 40 by sting parse.
	 */
	if (sb_ul != '\0') {
		if (sb_ul == 'l') {
			chspec_ch = UPPER_20_SB(ctl_ch);
			chspec_sb = WL_CHANSPEC_CTL_SB_LLL;
		} else if (sb_ul == 'u') {
			chspec_ch = LOWER_20_SB(ctl_ch);
			chspec_sb = WL_CHANSPEC_CTL_SB_LLU;
		}
	}
	/* if the bw is 20, center and sideband are trivial */
	else if (chspec_bw == WL_CHANSPEC_BW_20) {
		chspec_ch = ctl_ch;
		chspec_sb = WL_CHANSPEC_CTL_SB_NONE;
	}
	/* if the bw is 40/80/160, not 80+80, a single method
	 * can be used to to find the center and sideband
	 */
	else if (chspec_bw != WL_CHANSPEC_BW_8080) {
		/* figure out ctl sideband based on ctl channel and bandwidth */
		const uint8 *center_ch = NULL;
		int num_ch = 0;
		int sb = -1;

		if (chspec_bw == WL_CHANSPEC_BW_40) {
			center_ch = wf_5g_40m_chans;
			num_ch = WF_NUM_5G_40M_CHANS;
		} else if (chspec_bw == WL_CHANSPEC_BW_80) {
			center_ch = wf_5g_80m_chans;
			num_ch = WF_NUM_5G_80M_CHANS;
		} else if (chspec_bw == WL_CHANSPEC_BW_160) {
			center_ch = wf_5g_160m_chans;
			num_ch = WF_NUM_5G_160M_CHANS;
		} else {
			return 0;
		}

		for (i = 0; i < num_ch; i ++) {
			sb = channel_to_sb(center_ch[i], ctl_ch, bw);
			if (sb >= 0) {
				chspec_ch = center_ch[i];
				chspec_sb = sb << WL_CHANSPEC_CTL_SB_SHIFT;
				break;
			}
		}

		/* check for no matching sb/center */
		if (sb < 0) {
			return 0;
		}
	}
	/* Otherwise, bw is 80+80. Figure out channel pair and sb */
	else {
		int ch1_id = 0, ch2_id = 0;
		int sb;

		ch1_id = channel_80mhz_to_id(ch1);
		ch2_id = channel_80mhz_to_id(ch2);

		/* validate channels */
		if (ch1 >= ch2 || ch1_id < 0 || ch2_id < 0)
			return 0;

		/* combined channel in chspec */
		chspec_ch = (((uint16)ch1_id << WL_CHANSPEC_CHAN1_SHIFT) |
			((uint16)ch2_id << WL_CHANSPEC_CHAN2_SHIFT));

		/* figure out ctl sideband */

		/* does the primary channel fit with the 1st 80MHz channel ? */
		sb = channel_to_sb(ch1, ctl_ch, bw);
		if (sb < 0) {
			/* no, so does the primary channel fit with the 2nd 80MHz channel ? */
			sb = channel_to_sb(ch2, ctl_ch, bw);
			if (sb < 0) {
				/* no match for ctl_ch to either 80MHz center channel */
				return 0;
			}
			/* sb index is 0-3 for the low 80MHz channel, and 4-7 for
			 * the high 80MHz channel. Add 4 to to shift to high set.
			 */
			sb += 4;
		}

		chspec_sb = sb << WL_CHANSPEC_CTL_SB_SHIFT;
	}

	chspec = (chspec_ch | chspec_band | chspec_bw | chspec_sb);

	if (wf_chspec_malformed(chspec))
		return 0;

	return chspec;
}

/*
 * Verify the chanspec is using a legal set of parameters, i.e. that the
 * chanspec specified a band, bw, ctl_sb and channel and that the
 * combination could be legal given any set of circumstances.
 * RETURNS: TRUE is the chanspec is malformed, false if it looks good.
 */
bool
wf_chspec_malformed(chanspec_t chanspec)
{
	uint chspec_bw = CHSPEC_BW(chanspec);
	uint chspec_ch = CHSPEC_CHANNEL(chanspec);

	/* must be 2G or 5G band */
	if (CHSPEC_IS2G(chanspec)) {
		/* must be valid bandwidth */
		if (chspec_bw != WL_CHANSPEC_BW_20 &&
		    chspec_bw != WL_CHANSPEC_BW_40) {
			return TRUE;
		}
	} else if (CHSPEC_IS5G(chanspec)) {
		if (chspec_bw == WL_CHANSPEC_BW_8080) {
			uint ch1_id, ch2_id;

			/* channel number in 80+80 must be in range */
			ch1_id = CHSPEC_CHAN1(chanspec);
			ch2_id = CHSPEC_CHAN2(chanspec);
			if (ch1_id >= WF_NUM_5G_80M_CHANS || ch2_id >= WF_NUM_5G_80M_CHANS)
				return TRUE;

			/* ch2 must be above ch1 for the chanspec */
			if (ch2_id <= ch1_id)
				return TRUE;
		} else if (chspec_bw == WL_CHANSPEC_BW_20 || chspec_bw == WL_CHANSPEC_BW_40 ||
		           chspec_bw == WL_CHANSPEC_BW_80 || chspec_bw == WL_CHANSPEC_BW_160) {

			if (chspec_ch > MAXCHANNEL) {
				return TRUE;
			}
		} else {
			/* invalid bandwidth */
			return TRUE;
		}
	} else {
		/* must be 2G or 5G band */
		return TRUE;
	}

	/* side band needs to be consistent with bandwidth */
	if (chspec_bw == WL_CHANSPEC_BW_20) {
		if (CHSPEC_CTL_SB(chanspec) != WL_CHANSPEC_CTL_SB_LLL)
			return TRUE;
	} else if (chspec_bw == WL_CHANSPEC_BW_40) {
		if (CHSPEC_CTL_SB(chanspec) > WL_CHANSPEC_CTL_SB_LLU)
			return TRUE;
	} else if (chspec_bw == WL_CHANSPEC_BW_80) {
		if (CHSPEC_CTL_SB(chanspec) > WL_CHANSPEC_CTL_SB_LUU)
			return TRUE;
	}

	return FALSE;
}

/*
 * Verify the chanspec specifies a valid channel according to 802.11.
 * RETURNS: TRUE if the chanspec is a valid 802.11 channel
 */
bool
wf_chspec_valid(chanspec_t chanspec)
{
	uint chspec_bw = CHSPEC_BW(chanspec);
	uint chspec_ch = CHSPEC_CHANNEL(chanspec);

	if (wf_chspec_malformed(chanspec))
		return FALSE;

	if (CHSPEC_IS2G(chanspec)) {
		/* must be valid bandwidth and channel range */
		if (chspec_bw == WL_CHANSPEC_BW_20) {
			if (chspec_ch >= 1 && chspec_ch <= 14)
				return TRUE;
		} else if (chspec_bw == WL_CHANSPEC_BW_40) {
			if (chspec_ch >= 3 && chspec_ch <= 11)
				return TRUE;
		}
	} else if (CHSPEC_IS5G(chanspec)) {
		if (chspec_bw == WL_CHANSPEC_BW_8080) {
			uint16 ch1, ch2;

			ch1 = wf_5g_80m_chans[CHSPEC_CHAN1(chanspec)];
			ch2 = wf_5g_80m_chans[CHSPEC_CHAN2(chanspec)];

			/* the two channels must be separated by more than 80MHz by VHT req,
			 * and ch2 above ch1 for the chanspec
			 */
			if (ch2 > ch1 + CH_80MHZ_APART)
				return TRUE;
		} else {
			const uint8 *center_ch;
			uint num_ch, i;

			if (chspec_bw == WL_CHANSPEC_BW_20 || chspec_bw == WL_CHANSPEC_BW_40) {
				center_ch = wf_5g_40m_chans;
				num_ch = WF_NUM_5G_40M_CHANS;
			} else if (chspec_bw == WL_CHANSPEC_BW_80) {
				center_ch = wf_5g_80m_chans;
				num_ch = WF_NUM_5G_80M_CHANS;
			} else if (chspec_bw == WL_CHANSPEC_BW_160) {
				center_ch = wf_5g_160m_chans;
				num_ch = WF_NUM_5G_160M_CHANS;
			} else {
				/* invalid bandwidth */
				return FALSE;
			}

			/* check for a valid center channel */
			if (chspec_bw == WL_CHANSPEC_BW_20) {
				/* We don't have an array of legal 20MHz 5G channels, but they are
				 * each side of the legal 40MHz channels.  Check the chanspec
				 * channel against either side of the 40MHz channels.
				 */
				for (i = 0; i < num_ch; i ++) {
					if (chspec_ch == (uint)LOWER_20_SB(center_ch[i]) ||
					    chspec_ch == (uint)UPPER_20_SB(center_ch[i]))
						break; /* match found */
				}

				if (i == num_ch) {
					/* check for channel 165 which is not the side band
					 * of 40MHz 5G channel
					 */
					if (chspec_ch == 165)
						i = 0;

					/* check for legacy JP channels on failure */
					if (chspec_ch == 34 || chspec_ch == 38 ||
					    chspec_ch == 42 || chspec_ch == 46)
						i = 0;
				}
			} else {
				/* check the chanspec channel to each legal channel */
				for (i = 0; i < num_ch; i ++) {
					if (chspec_ch == center_ch[i])
						break; /* match found */
				}
			}

			if (i < num_ch) {
				/* match found */
				return TRUE;
			}
		}
	}

	return FALSE;
}

/*
 * This function returns the channel number that control traffic is being sent on, for 20MHz
 * channels this is just the channel number, for 40MHZ, 80MHz, 160MHz channels it is the 20MHZ
 * sideband depending on the chanspec selected
 */
uint8
wf_chspec_ctlchan(chanspec_t chspec)
{
	uint center_chan;
	uint bw_mhz;
	uint sb;

	ASSERT(!wf_chspec_malformed(chspec));

	/* Is there a sideband ? */
	if (CHSPEC_IS20(chspec)) {
		return CHSPEC_CHANNEL(chspec);
	} else {
		sb = CHSPEC_CTL_SB(chspec) >> WL_CHANSPEC_CTL_SB_SHIFT;

		if (CHSPEC_IS8080(chspec)) {
			bw_mhz = 80;

			if (sb < 4) {
				center_chan = CHSPEC_CHAN1(chspec);
			}
			else {
				center_chan = CHSPEC_CHAN2(chspec);
				sb -= 4;
			}

			/* convert from channel index to channel number */
			center_chan = wf_5g_80m_chans[center_chan];
		}
		else {
			bw_mhz = bw_chspec_to_mhz(chspec);
			center_chan = CHSPEC_CHANNEL(chspec) >> WL_CHANSPEC_CHAN_SHIFT;
		}

		return (channel_to_ctl_chan(center_chan, bw_mhz, sb));
	}
}

/* given a chanspec, return the bandwidth string */
char *
wf_chspec_to_bw_str(chanspec_t chspec)
{
	return (char *)wf_chspec_bw_str[(CHSPEC_BW(chspec) >> WL_CHANSPEC_BW_SHIFT)];
}

/*
 * This function returns the chanspec of the control channel of a given chanspec
 */
chanspec_t
wf_chspec_ctlchspec(chanspec_t chspec)
{
	chanspec_t ctl_chspec = chspec;
	uint8 ctl_chan;

	ASSERT(!wf_chspec_malformed(chspec));

	/* Is there a sideband ? */
	if (!CHSPEC_IS20(chspec)) {
		ctl_chan = wf_chspec_ctlchan(chspec);
		ctl_chspec = ctl_chan | WL_CHANSPEC_BW_20;
		ctl_chspec |= CHSPEC_BAND(chspec);
	}
	return ctl_chspec;
}

/* return chanspec given control channel and bandwidth
 * return 0 on error
 */
uint16
wf_channel2chspec(uint ctl_ch, uint bw)
{
	uint16 chspec;
	const uint8 *center_ch = NULL;
	int num_ch = 0;
	int sb = -1;
	int i = 0;

	chspec = ((ctl_ch <= CH_MAX_2G_CHANNEL) ? WL_CHANSPEC_BAND_2G : WL_CHANSPEC_BAND_5G);

	chspec |= bw;

	if (bw == WL_CHANSPEC_BW_40) {
		center_ch = wf_5g_40m_chans;
		num_ch = WF_NUM_5G_40M_CHANS;
		bw = 40;
	} else if (bw == WL_CHANSPEC_BW_80) {
		center_ch = wf_5g_80m_chans;
		num_ch = WF_NUM_5G_80M_CHANS;
		bw = 80;
	} else if (bw == WL_CHANSPEC_BW_160) {
		center_ch = wf_5g_160m_chans;
		num_ch = WF_NUM_5G_160M_CHANS;
		bw = 160;
	} else if (bw == WL_CHANSPEC_BW_20) {
		chspec |= ctl_ch;
		return chspec;
	} else {
		return 0;
	}

	for (i = 0; i < num_ch; i ++) {
		sb = channel_to_sb(center_ch[i], ctl_ch, bw);
		if (sb >= 0) {
			chspec |= center_ch[i];
			chspec |= (sb << WL_CHANSPEC_CTL_SB_SHIFT);
			break;
		}
	}

	/* check for no matching sb/center */
	if (sb < 0) {
		return 0;
	}

	return chspec;
}

/*
 * This function returns the chanspec for the primary 40MHz of an 80MHz channel.
 * The control sideband specifies the same 20MHz channel that the 80MHz channel is using
 * as the primary 20MHz channel.
 */
extern chanspec_t wf_chspec_primary40_chspec(chanspec_t chspec)
{
	chanspec_t chspec40 = chspec;
	uint center_chan;
	uint sb;

	ASSERT(!wf_chspec_malformed(chspec));

	if (CHSPEC_IS80(chspec)) {
		center_chan = CHSPEC_CHANNEL(chspec);
		sb = CHSPEC_CTL_SB(chspec);

		if (sb == WL_CHANSPEC_CTL_SB_UL) {
			/* Primary 40MHz is on upper side */
			sb = WL_CHANSPEC_CTL_SB_L;
			center_chan += CH_20MHZ_APART;
		} else if (sb == WL_CHANSPEC_CTL_SB_UU) {
			/* Primary 40MHz is on upper side */
			sb = WL_CHANSPEC_CTL_SB_U;
			center_chan += CH_20MHZ_APART;
		} else {
			/* Primary 40MHz is on lower side */
			/* sideband bits are the same for LL/LU and L/U */
			center_chan -= CH_20MHZ_APART;
		}

		/* Create primary 40MHz chanspec */
		chspec40 = (WL_CHANSPEC_BAND_5G | WL_CHANSPEC_BW_40 |
		            sb | center_chan);
	}

	return chspec40;
}

/*
 * Return the channel number for a given frequency and base frequency.
 * The returned channel number is relative to the given base frequency.
 * If the given base frequency is zero, a base frequency of 5 GHz is assumed for
 * frequencies from 5 - 6 GHz, and 2.407 GHz is assumed for 2.4 - 2.5 GHz.
 *
 * Frequency is specified in MHz.
 * The base frequency is specified as (start_factor * 500 kHz).
 * Constants WF_CHAN_FACTOR_2_4_G, WF_CHAN_FACTOR_5_G are defined for
 * 2.4 GHz and 5 GHz bands.
 *
 * The returned channel will be in the range [1, 14] in the 2.4 GHz band
 * and [0, 200] otherwise.
 * -1 is returned if the start_factor is WF_CHAN_FACTOR_2_4_G and the
 * frequency is not a 2.4 GHz channel, or if the frequency is not and even
 * multiple of 5 MHz from the base frequency to the base plus 1 GHz.
 *
 * Reference 802.11 REVma, section 17.3.8.3, and 802.11B section 18.4.6.2
 */
int
wf_mhz2channel(uint freq, uint start_factor)
{
	int ch = -1;
	uint base;
	int offset;

	/* take the default channel start frequency */
	if (start_factor == 0) {
		if (freq >= 2400 && freq <= 2500)
			start_factor = WF_CHAN_FACTOR_2_4_G;
		else if (freq >= 5000 && freq <= 6000)
			start_factor = WF_CHAN_FACTOR_5_G;
	}

	if (freq == 2484 && start_factor == WF_CHAN_FACTOR_2_4_G)
		return 14;

	base = start_factor / 2;

	/* check that the frequency is in 1GHz range of the base */
	if ((freq < base) || (freq > base + 1000))
		return -1;

	offset = freq - base;
	ch = offset / 5;

	/* check that frequency is a 5MHz multiple from the base */
	if (offset != (ch * 5))
		return -1;

	/* restricted channel range check for 2.4G */
	if (start_factor == WF_CHAN_FACTOR_2_4_G && (ch < 1 || ch > 13))
		return -1;

	return ch;
}

/*
 * Return the center frequency in MHz of the given channel and base frequency.
 * The channel number is interpreted relative to the given base frequency.
 *
 * The valid channel range is [1, 14] in the 2.4 GHz band and [0, 200] otherwise.
 * The base frequency is specified as (start_factor * 500 kHz).
 * Constants WF_CHAN_FACTOR_2_4_G, WF_CHAN_FACTOR_4_G, and WF_CHAN_FACTOR_5_G
 * are defined for 2.4 GHz, 4 GHz, and 5 GHz bands.
 * The channel range of [1, 14] is only checked for a start_factor of
 * WF_CHAN_FACTOR_2_4_G (4814 = 2407 * 2).
 * Odd start_factors produce channels on .5 MHz boundaries, in which case
 * the answer is rounded down to an integral MHz.
 * -1 is returned for an out of range channel.
 *
 * Reference 802.11 REVma, section 17.3.8.3, and 802.11B section 18.4.6.2
 */
int
wf_channel2mhz(uint ch, uint start_factor)
{
	int freq;

	if ((start_factor == WF_CHAN_FACTOR_2_4_G && (ch < 1 || ch > 14)) ||
	    (ch > 200))
		freq = -1;
	else if ((start_factor == WF_CHAN_FACTOR_2_4_G) && (ch == 14))
		freq = 2484;
	else
		freq = ch * 5 + start_factor / 2;

	return freq;
}

/*
 * Returns the 80+80 chanspec corresponding to the following input parameters
 *
 *    primary_20mhz - Primary 20 Mhz channel
 *    chan1 - channel number of first 80 Mhz band
 *    chan2 - channel number of second 80 Mhz band
 *
 *  parameters chan1 and chan2  are channel numbers in {42, 58, 106, 122, 138, 155}
 *
 *  returns INVCHANSPEC in case of error
 */

chanspec_t
wf_chspec_get8080_chspec(uint8 primary_20mhz, uint8 chan1, uint8 chan2)
{
	int sb = 0;
	uint16 chanspec = 0;
	int chan1_id = 0, chan2_id = 0;

	/* does the primary channel fit with the 1st 80MHz channel ? */
	sb = channel_to_sb(chan1, primary_20mhz, 80);
	if (sb < 0) {
		/* no, so does the primary channel fit with the 2nd 80MHz channel ? */
		sb = channel_to_sb(chan2, primary_20mhz, 80);
		if (sb < 0) {
			/* no match for ctl_ch to either 80MHz center channel */
			return INVCHANSPEC;
		}
		/* sb index is 0-3 for the low 80MHz channel, and 4-7 for
		 * the high 80MHz channel. Add 4 to to shift to high set.
		 */
		sb += 4;
	}
	chan1_id = channel_80mhz_to_id(chan1);
	chan2_id = channel_80mhz_to_id(chan2);
	if (chan1_id == -1 || chan2_id == -1)
		return INVCHANSPEC;

	chanspec = (chan1_id << WL_CHANSPEC_CHAN1_SHIFT)|
		(chan2_id << WL_CHANSPEC_CHAN2_SHIFT)|
		(sb << WL_CHANSPEC_CTL_SB_SHIFT)|
		(WL_CHANSPEC_BW_8080)|
		(WL_CHANSPEC_BAND_5G);

	return chanspec;

}

/*
 * This function returns the 80Mhz channel for the given id.
 */
static uint8
wf_chspec_get80Mhz_ch(uint8 chan_80Mhz_id)
{
	if (chan_80Mhz_id < WF_NUM_5G_80M_CHANS)
		return wf_5g_80m_chans[chan_80Mhz_id];

	return 0;
}

/*
 * Returns the primary 80 Mhz channel for the provided chanspec
 *
 *    chanspec - Input chanspec for which the 80MHz primary channel has to be retrieved
 *
 *  returns -1 in case the provided channel is 20/40 Mhz chanspec
 */

uint8
wf_chspec_primary80_channel(chanspec_t chanspec)
{
	uint8 chan1 = 0, chan2 = 0, primary_20mhz = 0, primary80_chan = 0;
	int sb = 0;

	primary_20mhz = wf_chspec_ctlchan(chanspec);

	if (CHSPEC_IS80(chanspec))	{
		primary80_chan = CHSPEC_CHANNEL(chanspec);
	}
	else if (CHSPEC_IS8080(chanspec)) {
		chan1 = wf_chspec_get80Mhz_ch(CHSPEC_CHAN1(chanspec));
		chan2 = wf_chspec_get80Mhz_ch(CHSPEC_CHAN2(chanspec));

		/* does the primary channel fit with the 1st 80MHz channel ? */
		sb = channel_to_sb(chan1, primary_20mhz, 80);
		if (sb < 0) {
			/* no, so does the primary channel fit with the 2nd 80MHz channel ? */
			sb = channel_to_sb(chan2, primary_20mhz, 80);
			if (!(sb < 0)) {
				primary80_chan = chan2;
			}
		}
		else {
			primary80_chan = chan1;
		}
	}
	else if (CHSPEC_IS160(chanspec)) {
		chan1 = CHSPEC_CHANNEL(chanspec);
		sb = channel_to_sb(chan1, primary_20mhz, 160);
		if (!(sb < 0)) {
		    /* based on the sb value  primary 80 channel can be retrieved
			 * if sb is in range 0 to 3 the lower band is the 80Mhz primary band
			 */
			if (sb < 4) {
				primary80_chan = chan1 - CH_40MHZ_APART;
			}
			/* if sb is in range 4 to 7 the lower band is the 80Mhz primary band */
			else
			{
				primary80_chan = chan1 + CH_40MHZ_APART;
			}
		}
	}
	else {
		/* for 20 and 40 Mhz */
		primary80_chan = -1;
	}
	return primary80_chan;
}

/*
 * Returns the secondary 80 Mhz channel for the provided chanspec
 *
 *    chanspec - Input chanspec for which the 80MHz secondary channel has to be retrieved
 *
 *  returns -1 in case the provided channel is 20/40 Mhz chanspec
 */
uint8
wf_chspec_secondary80_channel(chanspec_t chanspec)
{
	uint8 chan1 = 0, chan2 = 0, primary_20mhz = 0, secondary80_chan = 0;
	int sb = 0;

	primary_20mhz = wf_chspec_ctlchan(chanspec);
	if (CHSPEC_IS80(chanspec)) {
		secondary80_chan = -1;
	}
	else if (CHSPEC_IS8080(chanspec)) {
		chan1 = wf_chspec_get80Mhz_ch(CHSPEC_CHAN1(chanspec));
		chan2 = wf_chspec_get80Mhz_ch(CHSPEC_CHAN2(chanspec));

		/* does the primary channel fit with the 1st 80MHz channel ? */
		sb = channel_to_sb(chan1, primary_20mhz, 80);
		if (sb < 0) {
			/* no, so does the primary channel fit with the 2nd 80MHz channel ? */
			sb = channel_to_sb(chan2, primary_20mhz, 80);
			if (!(sb < 0)) {
				secondary80_chan = chan1;
			}
		}
		else {
			secondary80_chan = chan2;
		}
	}
	else if (CHSPEC_IS160(chanspec)) {
		chan1 = CHSPEC_CHANNEL(chanspec);
		sb = channel_to_sb(chan1, primary_20mhz, 160);
		if (!(sb < 0)) {
		    /* based on the sb value  secondary 80 channel can be retrieved
			  *if sb is in range 0 to 3 upper band is the secondary 80Mhz  band
			  */
			if (sb < 4) {
				secondary80_chan = chan1 + CH_40MHZ_APART;
			}
			/* if sb is in range 4 to 7 the lower band is the secondary 80Mhz band */
			else
			{
				secondary80_chan = chan1 - CH_40MHZ_APART;
			}
		}
	}
	else {
		/* for 20 and 40 Mhz */
		secondary80_chan  = -1;
	}
	return secondary80_chan;
}

/*
 * This function returns the chanspec for the primary 80MHz of an 160MHz or 80+80 channel.
 *
 *    chanspec - Input chanspec for which the primary 80Mhz chanspec has to be retreived
 *
 *  returns INVCHANSPEC in case the provided channel is 20/40 Mhz chanspec
 */
chanspec_t
wf_chspec_primary80_chspec(chanspec_t chspec)
{
	chanspec_t chspec80;
	uint center_chan, chan1 = 0, chan2 = 0;
	uint sb;

	ASSERT(!wf_chspec_malformed(chspec));
	if (CHSPEC_IS8080(chspec)) {
		chan1 = wf_chspec_get80Mhz_ch(CHSPEC_CHAN1(chspec));
		chan2 = wf_chspec_get80Mhz_ch(CHSPEC_CHAN2(chspec));

		sb = CHSPEC_CTL_SB(chspec);

		if (sb < 4) {
			/* Primary 80MHz is on lower side */
			center_chan = chan1;
		}
		else
		{
			/* Primary 80MHz is on upper side */
			center_chan = chan2;
			sb -= 4;
		}
		/* Create primary 80MHz chanspec */
		chspec80 = (WL_CHANSPEC_BAND_5G | WL_CHANSPEC_BW_80 |sb | center_chan);
	}
	else if (CHSPEC_IS160(chspec)) {
		center_chan = CHSPEC_CHANNEL(chspec);
		sb = CHSPEC_CTL_SB(chspec);

		if (sb < 4) {
			/* Primary 80MHz is on upper side */
			center_chan -= CH_40MHZ_APART;
		}
		else
		{
			/* Primary 80MHz is on lower side */
			center_chan += CH_40MHZ_APART;
			sb -= 4;
		}
		/* Create primary 80MHz chanspec */
		chspec80 = (WL_CHANSPEC_BAND_5G | WL_CHANSPEC_BW_80 | sb | center_chan);
	}
	else
	{
		chspec80 = INVCHANSPEC;
	}
	return chspec80;
}
