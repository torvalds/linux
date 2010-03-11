/*
 *
 * Hardware accelerated Matrox PCI cards - G450/G550 PLL control.
 *
 * (c) 2001-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Portions Copyright (c) 2001 Matrox Graphics Inc.
 *
 * Version: 1.64 2002/06/10
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

#include "g450_pll.h"
#include "matroxfb_DAC1064.h"

static inline unsigned int g450_vco2f(unsigned char p, unsigned int fvco) {
	return (p & 0x40) ? fvco : fvco >> ((p & 3) + 1);
}

static inline unsigned int g450_f2vco(unsigned char p, unsigned int fin) {
	return (p & 0x40) ? fin : fin << ((p & 3) + 1);
}

static unsigned int g450_mnp2vco(const struct matrox_fb_info *minfo,
				 unsigned int mnp)
{
	unsigned int m, n;

	m = ((mnp >> 16) & 0x0FF) + 1;
	n = ((mnp >>  7) & 0x1FE) + 4;
	return (minfo->features.pll.ref_freq * n + (m >> 1)) / m;
}

unsigned int g450_mnp2f(const struct matrox_fb_info *minfo, unsigned int mnp)
{
	return g450_vco2f(mnp, g450_mnp2vco(minfo, mnp));
}

static inline unsigned int pll_freq_delta(unsigned int f1, unsigned int f2) {
	if (f2 < f1) {
    		f2 = f1 - f2;
	} else {
		f2 = f2 - f1;
	}
	return f2;
}

#define NO_MORE_MNP	0x01FFFFFF
#define G450_MNP_FREQBITS	(0xFFFFFF43)	/* do not mask high byte so we'll catch NO_MORE_MNP */

static unsigned int g450_nextpll(const struct matrox_fb_info *minfo,
				 const struct matrox_pll_limits *pi,
				 unsigned int *fvco, unsigned int mnp)
{
	unsigned int m, n, p;
	unsigned int tvco = *fvco;

	m = (mnp >> 16) & 0xFF;
	p = mnp & 0xFF;

	do {
		if (m == 0 || m == 0xFF) {
			if (m == 0) {
				if (p & 0x40) {
					return NO_MORE_MNP;
				}
			        if (p & 3) {
					p--;
				} else {
					p = 0x40;
				}
				tvco >>= 1;
				if (tvco < pi->vcomin) {
					return NO_MORE_MNP;
				}
				*fvco = tvco;
			}

			p &= 0x43;
			if (tvco < 550000) {
/*				p |= 0x00; */
			} else if (tvco < 700000) {
				p |= 0x08;
			} else if (tvco < 1000000) {
				p |= 0x10;
			} else if (tvco < 1150000) {
				p |= 0x18;
			} else {
				p |= 0x20;
			}
			m = 9;
		} else {
			m--;
		}
		n = ((tvco * (m+1) + minfo->features.pll.ref_freq) / (minfo->features.pll.ref_freq * 2)) - 2;
	} while (n < 0x03 || n > 0x7A);
	return (m << 16) | (n << 8) | p;
}

static unsigned int g450_firstpll(const struct matrox_fb_info *minfo,
				  const struct matrox_pll_limits *pi,
				  unsigned int *vco, unsigned int fout)
{
	unsigned int p;
	unsigned int vcomax;

	vcomax = pi->vcomax;
	if (fout > (vcomax / 2)) {
		if (fout > vcomax) {
			*vco = vcomax;
		} else {
			*vco = fout;
		}
		p = 0x40;
	} else {
		unsigned int tvco;

		p = 3;
		tvco = g450_f2vco(p, fout);
		while (p && (tvco > vcomax)) {
			p--;
			tvco >>= 1;
		}
		if (tvco < pi->vcomin) {
			tvco = pi->vcomin;
		}
		*vco = tvco;
	}
	return g450_nextpll(minfo, pi, vco, 0xFF0000 | p);
}

static inline unsigned int g450_setpll(const struct matrox_fb_info *minfo,
				       unsigned int mnp, unsigned int pll)
{
	switch (pll) {
		case M_PIXEL_PLL_A:
			matroxfb_DAC_out(minfo, M1064_XPIXPLLAM, mnp >> 16);
			matroxfb_DAC_out(minfo, M1064_XPIXPLLAN, mnp >> 8);
			matroxfb_DAC_out(minfo, M1064_XPIXPLLAP, mnp);
			return M1064_XPIXPLLSTAT;

		case M_PIXEL_PLL_B:
			matroxfb_DAC_out(minfo, M1064_XPIXPLLBM, mnp >> 16);
			matroxfb_DAC_out(minfo, M1064_XPIXPLLBN, mnp >> 8);
			matroxfb_DAC_out(minfo, M1064_XPIXPLLBP, mnp);
			return M1064_XPIXPLLSTAT;

		case M_PIXEL_PLL_C:
			matroxfb_DAC_out(minfo, M1064_XPIXPLLCM, mnp >> 16);
			matroxfb_DAC_out(minfo, M1064_XPIXPLLCN, mnp >> 8);
			matroxfb_DAC_out(minfo, M1064_XPIXPLLCP, mnp);
			return M1064_XPIXPLLSTAT;

		case M_SYSTEM_PLL:
			matroxfb_DAC_out(minfo, DAC1064_XSYSPLLM, mnp >> 16);
			matroxfb_DAC_out(minfo, DAC1064_XSYSPLLN, mnp >> 8);
			matroxfb_DAC_out(minfo, DAC1064_XSYSPLLP, mnp);
			return DAC1064_XSYSPLLSTAT;

		case M_VIDEO_PLL:
			matroxfb_DAC_out(minfo, M1064_XVIDPLLM, mnp >> 16);
			matroxfb_DAC_out(minfo, M1064_XVIDPLLN, mnp >> 8);
			matroxfb_DAC_out(minfo, M1064_XVIDPLLP, mnp);
			return M1064_XVIDPLLSTAT;
	}
	return 0;
}

static inline unsigned int g450_cmppll(const struct matrox_fb_info *minfo,
				       unsigned int mnp, unsigned int pll)
{
	unsigned char m = mnp >> 16;
	unsigned char n = mnp >> 8;
	unsigned char p = mnp;

	switch (pll) {
		case M_PIXEL_PLL_A:
			return (matroxfb_DAC_in(minfo, M1064_XPIXPLLAM) != m ||
				matroxfb_DAC_in(minfo, M1064_XPIXPLLAN) != n ||
				matroxfb_DAC_in(minfo, M1064_XPIXPLLAP) != p);

		case M_PIXEL_PLL_B:
			return (matroxfb_DAC_in(minfo, M1064_XPIXPLLBM) != m ||
				matroxfb_DAC_in(minfo, M1064_XPIXPLLBN) != n ||
				matroxfb_DAC_in(minfo, M1064_XPIXPLLBP) != p);

		case M_PIXEL_PLL_C:
			return (matroxfb_DAC_in(minfo, M1064_XPIXPLLCM) != m ||
				matroxfb_DAC_in(minfo, M1064_XPIXPLLCN) != n ||
				matroxfb_DAC_in(minfo, M1064_XPIXPLLCP) != p);

		case M_SYSTEM_PLL:
			return (matroxfb_DAC_in(minfo, DAC1064_XSYSPLLM) != m ||
				matroxfb_DAC_in(minfo, DAC1064_XSYSPLLN) != n ||
				matroxfb_DAC_in(minfo, DAC1064_XSYSPLLP) != p);

		case M_VIDEO_PLL:
			return (matroxfb_DAC_in(minfo, M1064_XVIDPLLM) != m ||
				matroxfb_DAC_in(minfo, M1064_XVIDPLLN) != n ||
				matroxfb_DAC_in(minfo, M1064_XVIDPLLP) != p);
	}
	return 1;
}

static inline int g450_isplllocked(const struct matrox_fb_info *minfo,
				   unsigned int regidx)
{
	unsigned int j;

	for (j = 0; j < 1000; j++) {
		if (matroxfb_DAC_in(minfo, regidx) & 0x40) {
			unsigned int r = 0;
			int i;

			for (i = 0; i < 100; i++) {
				r += matroxfb_DAC_in(minfo, regidx) & 0x40;
			}
			return r >= (90 * 0x40);
		}
		/* udelay(1)... but DAC_in is much slower... */
	}
	return 0;
}

static int g450_testpll(const struct matrox_fb_info *minfo, unsigned int mnp,
			unsigned int pll)
{
	return g450_isplllocked(minfo, g450_setpll(minfo, mnp, pll));
}

static void updatehwstate_clk(struct matrox_hw_state* hw, unsigned int mnp, unsigned int pll) {
	switch (pll) {
		case M_SYSTEM_PLL:
			hw->DACclk[3] = mnp >> 16;
			hw->DACclk[4] = mnp >> 8;
			hw->DACclk[5] = mnp;
			break;
	}
}

void matroxfb_g450_setpll_cond(struct matrox_fb_info *minfo, unsigned int mnp,
			       unsigned int pll)
{
	if (g450_cmppll(minfo, mnp, pll)) {
		g450_setpll(minfo, mnp, pll);
	}
}

static inline unsigned int g450_findworkingpll(struct matrox_fb_info *minfo,
					       unsigned int pll,
					       unsigned int *mnparray,
					       unsigned int mnpcount)
{
	unsigned int found = 0;
	unsigned int idx;
	unsigned int mnpfound = mnparray[0];
		
	for (idx = 0; idx < mnpcount; idx++) {
		unsigned int sarray[3];
		unsigned int *sptr;
		{
			unsigned int mnp;
		
			sptr = sarray;
			mnp = mnparray[idx];
			if (mnp & 0x38) {
				*sptr++ = mnp - 8;
			}
			if ((mnp & 0x38) != 0x38) {
				*sptr++ = mnp + 8;
			}
			*sptr = mnp;
		}
		while (sptr >= sarray) {
			unsigned int mnp = *sptr--;
		
			if (g450_testpll(minfo, mnp - 0x0300, pll) &&
			    g450_testpll(minfo, mnp + 0x0300, pll) &&
			    g450_testpll(minfo, mnp - 0x0200, pll) &&
			    g450_testpll(minfo, mnp + 0x0200, pll) &&
			    g450_testpll(minfo, mnp - 0x0100, pll) &&
			    g450_testpll(minfo, mnp + 0x0100, pll)) {
				if (g450_testpll(minfo, mnp, pll)) {
					return mnp;
				}
			} else if (!found && g450_testpll(minfo, mnp, pll)) {
				mnpfound = mnp;
				found = 1;
			}
		}
	}
	g450_setpll(minfo, mnpfound, pll);
	return mnpfound;
}

static void g450_addcache(struct matrox_pll_cache* ci, unsigned int mnp_key, unsigned int mnp_value) {
	if (++ci->valid > ARRAY_SIZE(ci->data)) {
		ci->valid = ARRAY_SIZE(ci->data);
	}
	memmove(ci->data + 1, ci->data, (ci->valid - 1) * sizeof(*ci->data));
	ci->data[0].mnp_key = mnp_key & G450_MNP_FREQBITS;
	ci->data[0].mnp_value = mnp_value;
}

static int g450_checkcache(struct matrox_fb_info *minfo,
			   struct matrox_pll_cache *ci, unsigned int mnp_key)
{
	unsigned int i;
	
	mnp_key &= G450_MNP_FREQBITS;
	for (i = 0; i < ci->valid; i++) {
		if (ci->data[i].mnp_key == mnp_key) {
			unsigned int mnp;
			
			mnp = ci->data[i].mnp_value;
			if (i) {
				memmove(ci->data + 1, ci->data, i * sizeof(*ci->data));
				ci->data[0].mnp_key = mnp_key;
				ci->data[0].mnp_value = mnp;
			}
			return mnp;
		}
	}
	return NO_MORE_MNP;
}

static int __g450_setclk(struct matrox_fb_info *minfo, unsigned int fout,
			 unsigned int pll, unsigned int *mnparray,
			 unsigned int *deltaarray)
{
	unsigned int mnpcount;
	unsigned int pixel_vco;
	const struct matrox_pll_limits* pi;
	struct matrox_pll_cache* ci;

	pixel_vco = 0;
	switch (pll) {
		case M_PIXEL_PLL_A:
		case M_PIXEL_PLL_B:
		case M_PIXEL_PLL_C:
			{
				u_int8_t tmp, xpwrctrl;
				unsigned long flags;
				
				matroxfb_DAC_lock_irqsave(flags);

				xpwrctrl = matroxfb_DAC_in(minfo, M1064_XPWRCTRL);
				matroxfb_DAC_out(minfo, M1064_XPWRCTRL, xpwrctrl & ~M1064_XPWRCTRL_PANELPDN);
				mga_outb(M_SEQ_INDEX, M_SEQ1);
				mga_outb(M_SEQ_DATA, mga_inb(M_SEQ_DATA) | M_SEQ1_SCROFF);
				tmp = matroxfb_DAC_in(minfo, M1064_XPIXCLKCTRL);
				tmp |= M1064_XPIXCLKCTRL_DIS;
				if (!(tmp & M1064_XPIXCLKCTRL_PLL_UP)) {
					tmp |= M1064_XPIXCLKCTRL_PLL_UP;
				}
				matroxfb_DAC_out(minfo, M1064_XPIXCLKCTRL, tmp);
				/* DVI PLL preferred for frequencies up to
				   panel link max, standard PLL otherwise */
				if (fout >= minfo->max_pixel_clock_panellink)
					tmp = 0;
				else tmp =
					M1064_XDVICLKCTRL_DVIDATAPATHSEL |
					M1064_XDVICLKCTRL_C1DVICLKSEL |
					M1064_XDVICLKCTRL_C1DVICLKEN |
					M1064_XDVICLKCTRL_DVILOOPCTL |
					M1064_XDVICLKCTRL_P1LOOPBWDTCTL;
                                /* Setting this breaks PC systems so don't do it */
				/* matroxfb_DAC_out(minfo, M1064_XDVICLKCTRL, tmp); */
				matroxfb_DAC_out(minfo, M1064_XPWRCTRL,
						 xpwrctrl);

				matroxfb_DAC_unlock_irqrestore(flags);
			}
			{
				u_int8_t misc;
		
				misc = mga_inb(M_MISC_REG_READ) & ~0x0C;
				switch (pll) {
					case M_PIXEL_PLL_A:
						break;
					case M_PIXEL_PLL_B:
						misc |=  0x04;
						break;
					default:
						misc |=  0x0C;
						break;
				}
				mga_outb(M_MISC_REG, misc);
			}
			pi = &minfo->limits.pixel;
			ci = &minfo->cache.pixel;
			break;
		case M_SYSTEM_PLL:
			{
				u_int32_t opt;

				pci_read_config_dword(minfo->pcidev, PCI_OPTION_REG, &opt);
				if (!(opt & 0x20)) {
					pci_write_config_dword(minfo->pcidev, PCI_OPTION_REG, opt | 0x20);
				}
			}
			pi = &minfo->limits.system;
			ci = &minfo->cache.system;
			break;
		case M_VIDEO_PLL:
			{
				u_int8_t tmp;
				unsigned int mnp;
				unsigned long flags;
				
				matroxfb_DAC_lock_irqsave(flags);
				tmp = matroxfb_DAC_in(minfo, M1064_XPWRCTRL);
				if (!(tmp & 2)) {
					matroxfb_DAC_out(minfo, M1064_XPWRCTRL, tmp | 2);
				}
				
				mnp = matroxfb_DAC_in(minfo, M1064_XPIXPLLCM) << 16;
				mnp |= matroxfb_DAC_in(minfo, M1064_XPIXPLLCN) << 8;
				pixel_vco = g450_mnp2vco(minfo, mnp);
				matroxfb_DAC_unlock_irqrestore(flags);
			}
			pi = &minfo->limits.video;
			ci = &minfo->cache.video;
			break;
		default:
			return -EINVAL;
	}

	mnpcount = 0;
	{
		unsigned int mnp;
		unsigned int xvco;

		for (mnp = g450_firstpll(minfo, pi, &xvco, fout); mnp != NO_MORE_MNP; mnp = g450_nextpll(minfo, pi, &xvco, mnp)) {
			unsigned int idx;
			unsigned int vco;
			unsigned int delta;

			vco = g450_mnp2vco(minfo, mnp);
#if 0			
			if (pll == M_VIDEO_PLL) {
				unsigned int big, small;

				if (vco < pixel_vco) {
					small = vco;
					big = pixel_vco;
				} else {
					small = pixel_vco;
					big = vco;
				}
				while (big > small) {
					big >>= 1;
				}
				if (big == small) {
					continue;
				}
			}
#endif			
			delta = pll_freq_delta(fout, g450_vco2f(mnp, vco));
			for (idx = mnpcount; idx > 0; idx--) {
				/* == is important; due to nextpll algorithm we get
				   sorted equally good frequencies from lower VCO 
				   frequency to higher - with <= lowest wins, while
				   with < highest one wins */
				if (delta <= deltaarray[idx-1]) {
					/* all else being equal except VCO,
					 * choose VCO not near (within 1/16th or so) VCOmin
					 * (freqs near VCOmin aren't as stable)
					 */
					if (delta == deltaarray[idx-1]
					    && vco != g450_mnp2vco(minfo, mnparray[idx-1])
					    && vco < (pi->vcomin * 17 / 16)) {
						break;
					}
					mnparray[idx] = mnparray[idx-1];
					deltaarray[idx] = deltaarray[idx-1];
				} else {
					break;
				}
			}
			mnparray[idx] = mnp;
			deltaarray[idx] = delta;
			mnpcount++;
		}
	}
	/* VideoPLL and PixelPLL matched: do nothing... In all other cases we should get at least one frequency */
	if (!mnpcount) {
		return -EBUSY;
	}
	{
		unsigned long flags;
		unsigned int mnp;
		
		matroxfb_DAC_lock_irqsave(flags);
		mnp = g450_checkcache(minfo, ci, mnparray[0]);
		if (mnp != NO_MORE_MNP) {
			matroxfb_g450_setpll_cond(minfo, mnp, pll);
		} else {
			mnp = g450_findworkingpll(minfo, pll, mnparray, mnpcount);
			g450_addcache(ci, mnparray[0], mnp);
		}
		updatehwstate_clk(&minfo->hw, mnp, pll);
		matroxfb_DAC_unlock_irqrestore(flags);
		return mnp;
	}
}

/* It must be greater than number of possible PLL values.
 * Currently there is 5(p) * 10(m) = 50 possible values. */
#define MNP_TABLE_SIZE  64

int matroxfb_g450_setclk(struct matrox_fb_info *minfo, unsigned int fout,
			 unsigned int pll)
{
	unsigned int* arr;
	
	arr = kmalloc(sizeof(*arr) * MNP_TABLE_SIZE * 2, GFP_KERNEL);
	if (arr) {
		int r;

		r = __g450_setclk(minfo, fout, pll, arr, arr + MNP_TABLE_SIZE);
		kfree(arr);
		return r;
	}
	return -ENOMEM;
}

EXPORT_SYMBOL(matroxfb_g450_setclk);
EXPORT_SYMBOL(g450_mnp2f);
EXPORT_SYMBOL(matroxfb_g450_setpll_cond);

MODULE_AUTHOR("(c) 2001-2002 Petr Vandrovec <vandrove@vc.cvut.cz>");
MODULE_DESCRIPTION("Matrox G450/G550 PLL driver");

MODULE_LICENSE("GPL");
