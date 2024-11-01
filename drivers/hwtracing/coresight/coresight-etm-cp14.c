// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <asm/hardware/cp14.h>

#include "coresight-etm.h"

int etm_readl_cp14(u32 reg, unsigned int *val)
{
	switch (reg) {
	case ETMCR:
		*val = etm_read(ETMCR);
		return 0;
	case ETMCCR:
		*val = etm_read(ETMCCR);
		return 0;
	case ETMTRIGGER:
		*val = etm_read(ETMTRIGGER);
		return 0;
	case ETMSR:
		*val = etm_read(ETMSR);
		return 0;
	case ETMSCR:
		*val = etm_read(ETMSCR);
		return 0;
	case ETMTSSCR:
		*val = etm_read(ETMTSSCR);
		return 0;
	case ETMTEEVR:
		*val = etm_read(ETMTEEVR);
		return 0;
	case ETMTECR1:
		*val = etm_read(ETMTECR1);
		return 0;
	case ETMFFLR:
		*val = etm_read(ETMFFLR);
		return 0;
	case ETMACVRn(0):
		*val = etm_read(ETMACVR0);
		return 0;
	case ETMACVRn(1):
		*val = etm_read(ETMACVR1);
		return 0;
	case ETMACVRn(2):
		*val = etm_read(ETMACVR2);
		return 0;
	case ETMACVRn(3):
		*val = etm_read(ETMACVR3);
		return 0;
	case ETMACVRn(4):
		*val = etm_read(ETMACVR4);
		return 0;
	case ETMACVRn(5):
		*val = etm_read(ETMACVR5);
		return 0;
	case ETMACVRn(6):
		*val = etm_read(ETMACVR6);
		return 0;
	case ETMACVRn(7):
		*val = etm_read(ETMACVR7);
		return 0;
	case ETMACVRn(8):
		*val = etm_read(ETMACVR8);
		return 0;
	case ETMACVRn(9):
		*val = etm_read(ETMACVR9);
		return 0;
	case ETMACVRn(10):
		*val = etm_read(ETMACVR10);
		return 0;
	case ETMACVRn(11):
		*val = etm_read(ETMACVR11);
		return 0;
	case ETMACVRn(12):
		*val = etm_read(ETMACVR12);
		return 0;
	case ETMACVRn(13):
		*val = etm_read(ETMACVR13);
		return 0;
	case ETMACVRn(14):
		*val = etm_read(ETMACVR14);
		return 0;
	case ETMACVRn(15):
		*val = etm_read(ETMACVR15);
		return 0;
	case ETMACTRn(0):
		*val = etm_read(ETMACTR0);
		return 0;
	case ETMACTRn(1):
		*val = etm_read(ETMACTR1);
		return 0;
	case ETMACTRn(2):
		*val = etm_read(ETMACTR2);
		return 0;
	case ETMACTRn(3):
		*val = etm_read(ETMACTR3);
		return 0;
	case ETMACTRn(4):
		*val = etm_read(ETMACTR4);
		return 0;
	case ETMACTRn(5):
		*val = etm_read(ETMACTR5);
		return 0;
	case ETMACTRn(6):
		*val = etm_read(ETMACTR6);
		return 0;
	case ETMACTRn(7):
		*val = etm_read(ETMACTR7);
		return 0;
	case ETMACTRn(8):
		*val = etm_read(ETMACTR8);
		return 0;
	case ETMACTRn(9):
		*val = etm_read(ETMACTR9);
		return 0;
	case ETMACTRn(10):
		*val = etm_read(ETMACTR10);
		return 0;
	case ETMACTRn(11):
		*val = etm_read(ETMACTR11);
		return 0;
	case ETMACTRn(12):
		*val = etm_read(ETMACTR12);
		return 0;
	case ETMACTRn(13):
		*val = etm_read(ETMACTR13);
		return 0;
	case ETMACTRn(14):
		*val = etm_read(ETMACTR14);
		return 0;
	case ETMACTRn(15):
		*val = etm_read(ETMACTR15);
		return 0;
	case ETMCNTRLDVRn(0):
		*val = etm_read(ETMCNTRLDVR0);
		return 0;
	case ETMCNTRLDVRn(1):
		*val = etm_read(ETMCNTRLDVR1);
		return 0;
	case ETMCNTRLDVRn(2):
		*val = etm_read(ETMCNTRLDVR2);
		return 0;
	case ETMCNTRLDVRn(3):
		*val = etm_read(ETMCNTRLDVR3);
		return 0;
	case ETMCNTENRn(0):
		*val = etm_read(ETMCNTENR0);
		return 0;
	case ETMCNTENRn(1):
		*val = etm_read(ETMCNTENR1);
		return 0;
	case ETMCNTENRn(2):
		*val = etm_read(ETMCNTENR2);
		return 0;
	case ETMCNTENRn(3):
		*val = etm_read(ETMCNTENR3);
		return 0;
	case ETMCNTRLDEVRn(0):
		*val = etm_read(ETMCNTRLDEVR0);
		return 0;
	case ETMCNTRLDEVRn(1):
		*val = etm_read(ETMCNTRLDEVR1);
		return 0;
	case ETMCNTRLDEVRn(2):
		*val = etm_read(ETMCNTRLDEVR2);
		return 0;
	case ETMCNTRLDEVRn(3):
		*val = etm_read(ETMCNTRLDEVR3);
		return 0;
	case ETMCNTVRn(0):
		*val = etm_read(ETMCNTVR0);
		return 0;
	case ETMCNTVRn(1):
		*val = etm_read(ETMCNTVR1);
		return 0;
	case ETMCNTVRn(2):
		*val = etm_read(ETMCNTVR2);
		return 0;
	case ETMCNTVRn(3):
		*val = etm_read(ETMCNTVR3);
		return 0;
	case ETMSQ12EVR:
		*val = etm_read(ETMSQ12EVR);
		return 0;
	case ETMSQ21EVR:
		*val = etm_read(ETMSQ21EVR);
		return 0;
	case ETMSQ23EVR:
		*val = etm_read(ETMSQ23EVR);
		return 0;
	case ETMSQ31EVR:
		*val = etm_read(ETMSQ31EVR);
		return 0;
	case ETMSQ32EVR:
		*val = etm_read(ETMSQ32EVR);
		return 0;
	case ETMSQ13EVR:
		*val = etm_read(ETMSQ13EVR);
		return 0;
	case ETMSQR:
		*val = etm_read(ETMSQR);
		return 0;
	case ETMEXTOUTEVRn(0):
		*val = etm_read(ETMEXTOUTEVR0);
		return 0;
	case ETMEXTOUTEVRn(1):
		*val = etm_read(ETMEXTOUTEVR1);
		return 0;
	case ETMEXTOUTEVRn(2):
		*val = etm_read(ETMEXTOUTEVR2);
		return 0;
	case ETMEXTOUTEVRn(3):
		*val = etm_read(ETMEXTOUTEVR3);
		return 0;
	case ETMCIDCVRn(0):
		*val = etm_read(ETMCIDCVR0);
		return 0;
	case ETMCIDCVRn(1):
		*val = etm_read(ETMCIDCVR1);
		return 0;
	case ETMCIDCVRn(2):
		*val = etm_read(ETMCIDCVR2);
		return 0;
	case ETMCIDCMR:
		*val = etm_read(ETMCIDCMR);
		return 0;
	case ETMIMPSPEC0:
		*val = etm_read(ETMIMPSPEC0);
		return 0;
	case ETMIMPSPEC1:
		*val = etm_read(ETMIMPSPEC1);
		return 0;
	case ETMIMPSPEC2:
		*val = etm_read(ETMIMPSPEC2);
		return 0;
	case ETMIMPSPEC3:
		*val = etm_read(ETMIMPSPEC3);
		return 0;
	case ETMIMPSPEC4:
		*val = etm_read(ETMIMPSPEC4);
		return 0;
	case ETMIMPSPEC5:
		*val = etm_read(ETMIMPSPEC5);
		return 0;
	case ETMIMPSPEC6:
		*val = etm_read(ETMIMPSPEC6);
		return 0;
	case ETMIMPSPEC7:
		*val = etm_read(ETMIMPSPEC7);
		return 0;
	case ETMSYNCFR:
		*val = etm_read(ETMSYNCFR);
		return 0;
	case ETMIDR:
		*val = etm_read(ETMIDR);
		return 0;
	case ETMCCER:
		*val = etm_read(ETMCCER);
		return 0;
	case ETMEXTINSELR:
		*val = etm_read(ETMEXTINSELR);
		return 0;
	case ETMTESSEICR:
		*val = etm_read(ETMTESSEICR);
		return 0;
	case ETMEIBCR:
		*val = etm_read(ETMEIBCR);
		return 0;
	case ETMTSEVR:
		*val = etm_read(ETMTSEVR);
		return 0;
	case ETMAUXCR:
		*val = etm_read(ETMAUXCR);
		return 0;
	case ETMTRACEIDR:
		*val = etm_read(ETMTRACEIDR);
		return 0;
	case ETMVMIDCVR:
		*val = etm_read(ETMVMIDCVR);
		return 0;
	case ETMOSLSR:
		*val = etm_read(ETMOSLSR);
		return 0;
	case ETMOSSRR:
		*val = etm_read(ETMOSSRR);
		return 0;
	case ETMPDCR:
		*val = etm_read(ETMPDCR);
		return 0;
	case ETMPDSR:
		*val = etm_read(ETMPDSR);
		return 0;
	default:
		*val = 0;
		return -EINVAL;
	}
}

int etm_writel_cp14(u32 reg, u32 val)
{
	switch (reg) {
	case ETMCR:
		etm_write(val, ETMCR);
		break;
	case ETMTRIGGER:
		etm_write(val, ETMTRIGGER);
		break;
	case ETMSR:
		etm_write(val, ETMSR);
		break;
	case ETMTSSCR:
		etm_write(val, ETMTSSCR);
		break;
	case ETMTEEVR:
		etm_write(val, ETMTEEVR);
		break;
	case ETMTECR1:
		etm_write(val, ETMTECR1);
		break;
	case ETMFFLR:
		etm_write(val, ETMFFLR);
		break;
	case ETMACVRn(0):
		etm_write(val, ETMACVR0);
		break;
	case ETMACVRn(1):
		etm_write(val, ETMACVR1);
		break;
	case ETMACVRn(2):
		etm_write(val, ETMACVR2);
		break;
	case ETMACVRn(3):
		etm_write(val, ETMACVR3);
		break;
	case ETMACVRn(4):
		etm_write(val, ETMACVR4);
		break;
	case ETMACVRn(5):
		etm_write(val, ETMACVR5);
		break;
	case ETMACVRn(6):
		etm_write(val, ETMACVR6);
		break;
	case ETMACVRn(7):
		etm_write(val, ETMACVR7);
		break;
	case ETMACVRn(8):
		etm_write(val, ETMACVR8);
		break;
	case ETMACVRn(9):
		etm_write(val, ETMACVR9);
		break;
	case ETMACVRn(10):
		etm_write(val, ETMACVR10);
		break;
	case ETMACVRn(11):
		etm_write(val, ETMACVR11);
		break;
	case ETMACVRn(12):
		etm_write(val, ETMACVR12);
		break;
	case ETMACVRn(13):
		etm_write(val, ETMACVR13);
		break;
	case ETMACVRn(14):
		etm_write(val, ETMACVR14);
		break;
	case ETMACVRn(15):
		etm_write(val, ETMACVR15);
		break;
	case ETMACTRn(0):
		etm_write(val, ETMACTR0);
		break;
	case ETMACTRn(1):
		etm_write(val, ETMACTR1);
		break;
	case ETMACTRn(2):
		etm_write(val, ETMACTR2);
		break;
	case ETMACTRn(3):
		etm_write(val, ETMACTR3);
		break;
	case ETMACTRn(4):
		etm_write(val, ETMACTR4);
		break;
	case ETMACTRn(5):
		etm_write(val, ETMACTR5);
		break;
	case ETMACTRn(6):
		etm_write(val, ETMACTR6);
		break;
	case ETMACTRn(7):
		etm_write(val, ETMACTR7);
		break;
	case ETMACTRn(8):
		etm_write(val, ETMACTR8);
		break;
	case ETMACTRn(9):
		etm_write(val, ETMACTR9);
		break;
	case ETMACTRn(10):
		etm_write(val, ETMACTR10);
		break;
	case ETMACTRn(11):
		etm_write(val, ETMACTR11);
		break;
	case ETMACTRn(12):
		etm_write(val, ETMACTR12);
		break;
	case ETMACTRn(13):
		etm_write(val, ETMACTR13);
		break;
	case ETMACTRn(14):
		etm_write(val, ETMACTR14);
		break;
	case ETMACTRn(15):
		etm_write(val, ETMACTR15);
		break;
	case ETMCNTRLDVRn(0):
		etm_write(val, ETMCNTRLDVR0);
		break;
	case ETMCNTRLDVRn(1):
		etm_write(val, ETMCNTRLDVR1);
		break;
	case ETMCNTRLDVRn(2):
		etm_write(val, ETMCNTRLDVR2);
		break;
	case ETMCNTRLDVRn(3):
		etm_write(val, ETMCNTRLDVR3);
		break;
	case ETMCNTENRn(0):
		etm_write(val, ETMCNTENR0);
		break;
	case ETMCNTENRn(1):
		etm_write(val, ETMCNTENR1);
		break;
	case ETMCNTENRn(2):
		etm_write(val, ETMCNTENR2);
		break;
	case ETMCNTENRn(3):
		etm_write(val, ETMCNTENR3);
		break;
	case ETMCNTRLDEVRn(0):
		etm_write(val, ETMCNTRLDEVR0);
		break;
	case ETMCNTRLDEVRn(1):
		etm_write(val, ETMCNTRLDEVR1);
		break;
	case ETMCNTRLDEVRn(2):
		etm_write(val, ETMCNTRLDEVR2);
		break;
	case ETMCNTRLDEVRn(3):
		etm_write(val, ETMCNTRLDEVR3);
		break;
	case ETMCNTVRn(0):
		etm_write(val, ETMCNTVR0);
		break;
	case ETMCNTVRn(1):
		etm_write(val, ETMCNTVR1);
		break;
	case ETMCNTVRn(2):
		etm_write(val, ETMCNTVR2);
		break;
	case ETMCNTVRn(3):
		etm_write(val, ETMCNTVR3);
		break;
	case ETMSQ12EVR:
		etm_write(val, ETMSQ12EVR);
		break;
	case ETMSQ21EVR:
		etm_write(val, ETMSQ21EVR);
		break;
	case ETMSQ23EVR:
		etm_write(val, ETMSQ23EVR);
		break;
	case ETMSQ31EVR:
		etm_write(val, ETMSQ31EVR);
		break;
	case ETMSQ32EVR:
		etm_write(val, ETMSQ32EVR);
		break;
	case ETMSQ13EVR:
		etm_write(val, ETMSQ13EVR);
		break;
	case ETMSQR:
		etm_write(val, ETMSQR);
		break;
	case ETMEXTOUTEVRn(0):
		etm_write(val, ETMEXTOUTEVR0);
		break;
	case ETMEXTOUTEVRn(1):
		etm_write(val, ETMEXTOUTEVR1);
		break;
	case ETMEXTOUTEVRn(2):
		etm_write(val, ETMEXTOUTEVR2);
		break;
	case ETMEXTOUTEVRn(3):
		etm_write(val, ETMEXTOUTEVR3);
		break;
	case ETMCIDCVRn(0):
		etm_write(val, ETMCIDCVR0);
		break;
	case ETMCIDCVRn(1):
		etm_write(val, ETMCIDCVR1);
		break;
	case ETMCIDCVRn(2):
		etm_write(val, ETMCIDCVR2);
		break;
	case ETMCIDCMR:
		etm_write(val, ETMCIDCMR);
		break;
	case ETMIMPSPEC0:
		etm_write(val, ETMIMPSPEC0);
		break;
	case ETMIMPSPEC1:
		etm_write(val, ETMIMPSPEC1);
		break;
	case ETMIMPSPEC2:
		etm_write(val, ETMIMPSPEC2);
		break;
	case ETMIMPSPEC3:
		etm_write(val, ETMIMPSPEC3);
		break;
	case ETMIMPSPEC4:
		etm_write(val, ETMIMPSPEC4);
		break;
	case ETMIMPSPEC5:
		etm_write(val, ETMIMPSPEC5);
		break;
	case ETMIMPSPEC6:
		etm_write(val, ETMIMPSPEC6);
		break;
	case ETMIMPSPEC7:
		etm_write(val, ETMIMPSPEC7);
		break;
	case ETMSYNCFR:
		etm_write(val, ETMSYNCFR);
		break;
	case ETMEXTINSELR:
		etm_write(val, ETMEXTINSELR);
		break;
	case ETMTESSEICR:
		etm_write(val, ETMTESSEICR);
		break;
	case ETMEIBCR:
		etm_write(val, ETMEIBCR);
		break;
	case ETMTSEVR:
		etm_write(val, ETMTSEVR);
		break;
	case ETMAUXCR:
		etm_write(val, ETMAUXCR);
		break;
	case ETMTRACEIDR:
		etm_write(val, ETMTRACEIDR);
		break;
	case ETMVMIDCVR:
		etm_write(val, ETMVMIDCVR);
		break;
	case ETMOSLAR:
		etm_write(val, ETMOSLAR);
		break;
	case ETMOSSRR:
		etm_write(val, ETMOSSRR);
		break;
	case ETMPDCR:
		etm_write(val, ETMPDCR);
		break;
	case ETMPDSR:
		etm_write(val, ETMPDSR);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
