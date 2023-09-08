/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MFD internals for Renesas RZ/G2L MTU3 Core driver
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 */

#ifndef RZ_MTU3_MFD_H
#define RZ_MTU3_MFD_H

#define MTU_8BIT_CH_0(_tier, _nfcr, _tcr, _tcr2, _tmdr1, _tiorh, _tiorl, _tbtm) \
	{ \
		[RZ_MTU3_TIER] = _tier, \
		[RZ_MTU3_NFCR] = _nfcr, \
		[RZ_MTU3_TCR] = _tcr, \
		[RZ_MTU3_TCR2] = _tcr2, \
		[RZ_MTU3_TMDR1] = _tmdr1, \
		[RZ_MTU3_TIORH] = _tiorh, \
		[RZ_MTU3_TIORL] = _tiorl, \
		[RZ_MTU3_TBTM] = _tbtm \
	}

#define MTU_8BIT_CH_1_2(_tier, _nfcr, _tsr, _tcr, _tcr2, _tmdr1, _tior) \
	{ \
		[RZ_MTU3_TIER] = _tier, \
		[RZ_MTU3_NFCR] = _nfcr, \
		[RZ_MTU3_TSR] = _tsr, \
		[RZ_MTU3_TCR] = _tcr, \
		[RZ_MTU3_TCR2] = _tcr2, \
		[RZ_MTU3_TMDR1] = _tmdr1, \
		[RZ_MTU3_TIOR] = _tior \
	} \

#define MTU_8BIT_CH_3_4_6_7(_tier, _nfcr, _tsr, _tcr, _tcr2, _tmdr1, _tiorh, _tiorl, _tbtm) \
	{ \
		[RZ_MTU3_TIER] = _tier, \
		[RZ_MTU3_NFCR] = _nfcr, \
		[RZ_MTU3_TSR] = _tsr, \
		[RZ_MTU3_TCR] = _tcr, \
		[RZ_MTU3_TCR2] = _tcr2, \
		[RZ_MTU3_TMDR1] = _tmdr1, \
		[RZ_MTU3_TIORH] = _tiorh, \
		[RZ_MTU3_TIORL] = _tiorl, \
		[RZ_MTU3_TBTM] = _tbtm \
	} \

#define MTU_8BIT_CH_5(_tier, _nfcr, _tstr, _tcntcmpclr, _tcru, _tcr2u, _tioru, \
		      _tcrv, _tcr2v, _tiorv, _tcrw, _tcr2w, _tiorw) \
	{ \
		[RZ_MTU3_TIER] = _tier, \
		[RZ_MTU3_NFCR] = _nfcr, \
		[RZ_MTU3_TSTR] = _tstr, \
		[RZ_MTU3_TCNTCMPCLR] = _tcntcmpclr, \
		[RZ_MTU3_TCRU] = _tcru, \
		[RZ_MTU3_TCR2U] = _tcr2u, \
		[RZ_MTU3_TIORU] = _tioru, \
		[RZ_MTU3_TCRV] = _tcrv, \
		[RZ_MTU3_TCR2V] = _tcr2v, \
		[RZ_MTU3_TIORV] = _tiorv, \
		[RZ_MTU3_TCRW] = _tcrw, \
		[RZ_MTU3_TCR2W] = _tcr2w, \
		[RZ_MTU3_TIORW] = _tiorw \
	} \

#define MTU_8BIT_CH_8(_tier, _nfcr, _tcr, _tcr2, _tmdr1, _tiorh, _tiorl) \
	{ \
		[RZ_MTU3_TIER] = _tier, \
		[RZ_MTU3_NFCR] = _nfcr, \
		[RZ_MTU3_TCR] = _tcr, \
		[RZ_MTU3_TCR2] = _tcr2, \
		[RZ_MTU3_TMDR1] = _tmdr1, \
		[RZ_MTU3_TIORH] = _tiorh, \
		[RZ_MTU3_TIORL] = _tiorl \
	} \

#define MTU_16BIT_CH_0(_tcnt, _tgra, _tgrb, _tgrc, _tgrd, _tgre, _tgrf) \
	{ \
		[RZ_MTU3_TCNT] = _tcnt, \
		[RZ_MTU3_TGRA] = _tgra, \
		[RZ_MTU3_TGRB] = _tgrb, \
		[RZ_MTU3_TGRC] = _tgrc, \
		[RZ_MTU3_TGRD] = _tgrd, \
		[RZ_MTU3_TGRE] = _tgre, \
		[RZ_MTU3_TGRF] = _tgrf \
	}

#define MTU_16BIT_CH_1_2(_tcnt, _tgra, _tgrb) \
	{ \
		[RZ_MTU3_TCNT] = _tcnt, \
		[RZ_MTU3_TGRA] = _tgra, \
		[RZ_MTU3_TGRB] = _tgrb \
	}

#define MTU_16BIT_CH_3_6(_tcnt, _tgra, _tgrb, _tgrc, _tgrd, _tgre) \
	{ \
		[RZ_MTU3_TCNT] = _tcnt, \
		[RZ_MTU3_TGRA] = _tgra, \
		[RZ_MTU3_TGRB] = _tgrb, \
		[RZ_MTU3_TGRC] = _tgrc, \
		[RZ_MTU3_TGRD] = _tgrd, \
		[RZ_MTU3_TGRE] = _tgre \
	}

#define MTU_16BIT_CH_4_7(_tcnt, _tgra, _tgrb, _tgrc, _tgrd, _tgre, _tgrf, \
			  _tadcr, _tadcora, _tadcorb, _tadcobra, _tadcobrb) \
	{ \
		[RZ_MTU3_TCNT] = _tcnt, \
		[RZ_MTU3_TGRA] = _tgra, \
		[RZ_MTU3_TGRB] = _tgrb, \
		[RZ_MTU3_TGRC] = _tgrc, \
		[RZ_MTU3_TGRD] = _tgrd, \
		[RZ_MTU3_TGRE] = _tgre, \
		[RZ_MTU3_TGRF] = _tgrf, \
		[RZ_MTU3_TADCR] = _tadcr, \
		[RZ_MTU3_TADCORA] = _tadcora, \
		[RZ_MTU3_TADCORB] = _tadcorb, \
		[RZ_MTU3_TADCOBRA] = _tadcobra, \
		[RZ_MTU3_TADCOBRB] = _tadcobrb \
	}

#define MTU_16BIT_CH_5(_tcntu, _tgru, _tcntv, _tgrv, _tcntw, _tgrw) \
	{ \
		[RZ_MTU3_TCNTU] = _tcntu, \
		[RZ_MTU3_TGRU] = _tgru, \
		[RZ_MTU3_TCNTV] = _tcntv, \
		[RZ_MTU3_TGRV] = _tgrv, \
		[RZ_MTU3_TCNTW] = _tcntw, \
		[RZ_MTU3_TGRW] = _tgrw \
	}

#define MTU_32BIT_CH_1(_tcntlw, _tgralw, _tgrblw) \
	{ \
	       [RZ_MTU3_TCNTLW] = _tcntlw, \
	       [RZ_MTU3_TGRALW] = _tgralw, \
	       [RZ_MTU3_TGRBLW] = _tgrblw \
	}

#define MTU_32BIT_CH_8(_tcnt, _tgra, _tgrb, _tgrc, _tgrd) \
	{ \
	       [RZ_MTU3_TCNT] = _tcnt, \
	       [RZ_MTU3_TGRA] = _tgra, \
	       [RZ_MTU3_TGRB] = _tgrb, \
	       [RZ_MTU3_TGRC] = _tgrc, \
	       [RZ_MTU3_TGRD] = _tgrd \
	}

#endif
