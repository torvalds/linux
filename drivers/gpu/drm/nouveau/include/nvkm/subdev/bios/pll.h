/* SPDX-License-Identifier: MIT */
#ifndef __NVBIOS_PLL_H__
#define __NVBIOS_PLL_H__
/*XXX: kill me */
struct nvkm_pll_vals {
	union {
		struct {
#ifdef __BIG_ENDIAN
			uint8_t N1, M1, N2, M2;
#else
			uint8_t M1, N1, M2, N2;
#endif
		};
		struct {
			uint16_t NM1, NM2;
		} __attribute__((packed));
	};
	int log2P;

	int refclk;
};

/* these match types in pll limits table version 0x40,
 * nvkm uses them on all chipsets internally where a
 * specific pll needs to be referenced, but the exact
 * register isn't known.
 */
enum nvbios_pll_type {
	PLL_CORE   = 0x01,
	PLL_SHADER = 0x02,
	PLL_UNK03  = 0x03,
	PLL_MEMORY = 0x04,
	PLL_VDEC   = 0x05,
	PLL_UNK40  = 0x40,
	PLL_UNK41  = 0x41,
	PLL_UNK42  = 0x42,
	PLL_VPLL0  = 0x80,
	PLL_VPLL1  = 0x81,
	PLL_VPLL2  = 0x82,
	PLL_VPLL3  = 0x83,
	PLL_MAX    = 0xff
};

struct nvbios_pll {
	enum nvbios_pll_type type;
	u32 reg;
	u32 refclk;

	u8 min_p;
	u8 max_p;
	u8 bias_p;

	/*
	 * for most pre nv50 cards setting a log2P of 7 (the common max_log2p
	 * value) is no different to 6 (at least for vplls) so allowing the MNP
	 * calc to use 7 causes the generated clock to be out by a factor of 2.
	 * however, max_log2p cannot be fixed-up during parsing as the
	 * unmodified max_log2p value is still needed for setting mplls, hence
	 * an additional max_usable_log2p member
	 */
	u8 max_p_usable;

	struct {
		u32 min_freq;
		u32 max_freq;
		u32 min_inputfreq;
		u32 max_inputfreq;
		u8  min_m;
		u8  max_m;
		u8  min_n;
		u8  max_n;
	} vco1, vco2;
};

int nvbios_pll_parse(struct nvkm_bios *, u32 type, struct nvbios_pll *);
#endif
