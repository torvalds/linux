/*
 * Broadcom SiliconBackplane hardware register definitions.
 *
 * Copyright (C) 1999-2009, Broadcom Corporation
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
 * $Id: sbconfig.h,v 13.67.30.1 2008/05/07 20:17:27 Exp $
 */


#ifndef	_SBCONFIG_H
#define	_SBCONFIG_H


#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif


#define SB_BUS_SIZE		0x10000		
#define SB_BUS_BASE(b)		(SI_ENUM_BASE + (b) * SB_BUS_SIZE)
#define	SB_BUS_MAXCORES		(SB_BUS_SIZE / SI_CORE_SIZE)	


#define	SBCONFIGOFF		0xf00		
#define	SBCONFIGSIZE		256		

#define SBIPSFLAG		0x08
#define SBTPSFLAG		0x18
#define	SBTMERRLOGA		0x48		
#define	SBTMERRLOG		0x50		
#define SBADMATCH3		0x60
#define SBADMATCH2		0x68
#define SBADMATCH1		0x70
#define SBIMSTATE		0x90
#define SBINTVEC		0x94
#define SBTMSTATELOW		0x98
#define SBTMSTATEHIGH		0x9c
#define SBBWA0			0xa0
#define SBIMCONFIGLOW		0xa8
#define SBIMCONFIGHIGH		0xac
#define SBADMATCH0		0xb0
#define SBTMCONFIGLOW		0xb8
#define SBTMCONFIGHIGH		0xbc
#define SBBCONFIG		0xc0
#define SBBSTATE		0xc8
#define SBACTCNFG		0xd8
#define	SBFLAGST		0xe8
#define SBIDLOW			0xf8
#define SBIDHIGH		0xfc



#define SBIMERRLOGA		0xea8
#define SBIMERRLOG		0xeb0
#define SBTMPORTCONNID0		0xed8
#define SBTMPORTLOCK0		0xef8

#ifndef _LANGUAGE_ASSEMBLY

typedef volatile struct _sbconfig {
	uint32	PAD[2];
	uint32	sbipsflag;		
	uint32	PAD[3];
	uint32	sbtpsflag;		
	uint32	PAD[11];
	uint32	sbtmerrloga;		
	uint32	PAD;
	uint32	sbtmerrlog;		
	uint32	PAD[3];
	uint32	sbadmatch3;		
	uint32	PAD;
	uint32	sbadmatch2;		
	uint32	PAD;
	uint32	sbadmatch1;		
	uint32	PAD[7];
	uint32	sbimstate;		
	uint32	sbintvec;		
	uint32	sbtmstatelow;		
	uint32	sbtmstatehigh;		
	uint32	sbbwa0;			
	uint32	PAD;
	uint32	sbimconfiglow;		
	uint32	sbimconfighigh;		
	uint32	sbadmatch0;		
	uint32	PAD;
	uint32	sbtmconfiglow;		
	uint32	sbtmconfighigh;		
	uint32	sbbconfig;		
	uint32	PAD;
	uint32	sbbstate;		
	uint32	PAD[3];
	uint32	sbactcnfg;		
	uint32	PAD[3];
	uint32	sbflagst;		
	uint32	PAD[3];
	uint32	sbidlow;		
	uint32	sbidhigh;		
} sbconfig_t;

#endif 


#define	SBIPS_INT1_MASK		0x3f		
#define	SBIPS_INT1_SHIFT	0
#define	SBIPS_INT2_MASK		0x3f00		
#define	SBIPS_INT2_SHIFT	8
#define	SBIPS_INT3_MASK		0x3f0000	
#define	SBIPS_INT3_SHIFT	16
#define	SBIPS_INT4_MASK		0x3f000000	
#define	SBIPS_INT4_SHIFT	24


#define	SBTPS_NUM0_MASK		0x3f		
#define	SBTPS_F0EN0		0x40		


#define	SBTMEL_CM		0x00000007	
#define	SBTMEL_CI		0x0000ff00	
#define	SBTMEL_EC		0x0f000000	
#define	SBTMEL_ME		0x80000000	


#define	SBIM_PC			0xf		
#define	SBIM_AP_MASK		0x30		
#define	SBIM_AP_BOTH		0x00		
#define	SBIM_AP_TS		0x10		
#define	SBIM_AP_TK		0x20		
#define	SBIM_AP_RSV		0x30		
#define	SBIM_IBE		0x20000		
#define	SBIM_TO			0x40000		
#define	SBIM_BY			0x01800000	
#define	SBIM_RJ			0x02000000	


#define	SBTML_RESET		0x0001		
#define	SBTML_REJ_MASK		0x0006		
#define	SBTML_REJ		0x0002		
#define	SBTML_TMPREJ		0x0004		

#define	SBTML_SICF_SHIFT	16		


#define	SBTMH_SERR		0x0001		
#define	SBTMH_INT		0x0002		
#define	SBTMH_BUSY		0x0004		
#define	SBTMH_TO		0x0020		

#define	SBTMH_SISF_SHIFT	16		


#define	SBBWA_TAB0_MASK		0xffff		
#define	SBBWA_TAB1_MASK		0xffff		
#define	SBBWA_TAB1_SHIFT	16


#define	SBIMCL_STO_MASK		0x7		
#define	SBIMCL_RTO_MASK		0x70		
#define	SBIMCL_RTO_SHIFT	4
#define	SBIMCL_CID_MASK		0xff0000	
#define	SBIMCL_CID_SHIFT	16


#define	SBIMCH_IEM_MASK		0xc		
#define	SBIMCH_TEM_MASK		0x30		
#define	SBIMCH_TEM_SHIFT	4
#define	SBIMCH_BEM_MASK		0xc0		
#define	SBIMCH_BEM_SHIFT	6


#define	SBAM_TYPE_MASK		0x3		
#define	SBAM_AD64		0x4		
#define	SBAM_ADINT0_MASK	0xf8		
#define	SBAM_ADINT0_SHIFT	3
#define	SBAM_ADINT1_MASK	0x1f8		
#define	SBAM_ADINT1_SHIFT	3
#define	SBAM_ADINT2_MASK	0x1f8		
#define	SBAM_ADINT2_SHIFT	3
#define	SBAM_ADEN		0x400		
#define	SBAM_ADNEG		0x800		
#define	SBAM_BASE0_MASK		0xffffff00	
#define	SBAM_BASE0_SHIFT	8
#define	SBAM_BASE1_MASK		0xfffff000	
#define	SBAM_BASE1_SHIFT	12
#define	SBAM_BASE2_MASK		0xffff0000	
#define	SBAM_BASE2_SHIFT	16


#define	SBTMCL_CD_MASK		0xff		
#define	SBTMCL_CO_MASK		0xf800		
#define	SBTMCL_CO_SHIFT		11
#define	SBTMCL_IF_MASK		0xfc0000	
#define	SBTMCL_IF_SHIFT		18
#define	SBTMCL_IM_MASK		0x3000000	
#define	SBTMCL_IM_SHIFT		24


#define	SBTMCH_BM_MASK		0x3		
#define	SBTMCH_RM_MASK		0x3		
#define	SBTMCH_RM_SHIFT		2
#define	SBTMCH_SM_MASK		0x30		
#define	SBTMCH_SM_SHIFT		4
#define	SBTMCH_EM_MASK		0x300		
#define	SBTMCH_EM_SHIFT		8
#define	SBTMCH_IM_MASK		0xc00		
#define	SBTMCH_IM_SHIFT		10


#define	SBBC_LAT_MASK		0x3		
#define	SBBC_MAX0_MASK		0xf0000		
#define	SBBC_MAX0_SHIFT		16
#define	SBBC_MAX1_MASK		0xf00000	
#define	SBBC_MAX1_SHIFT		20


#define	SBBS_SRD		0x1		
#define	SBBS_HRD		0x2		


#define	SBIDL_CS_MASK		0x3		
#define	SBIDL_AR_MASK		0x38		
#define	SBIDL_AR_SHIFT		3
#define	SBIDL_SYNCH		0x40		
#define	SBIDL_INIT		0x80		
#define	SBIDL_MINLAT_MASK	0xf00		
#define	SBIDL_MINLAT_SHIFT	8
#define	SBIDL_MAXLAT		0xf000		
#define	SBIDL_MAXLAT_SHIFT	12
#define	SBIDL_FIRST		0x10000		
#define	SBIDL_CW_MASK		0xc0000		
#define	SBIDL_CW_SHIFT		18
#define	SBIDL_TP_MASK		0xf00000	
#define	SBIDL_TP_SHIFT		20
#define	SBIDL_IP_MASK		0xf000000	
#define	SBIDL_IP_SHIFT		24
#define	SBIDL_RV_MASK		0xf0000000	
#define	SBIDL_RV_SHIFT		28
#define	SBIDL_RV_2_2		0x00000000	
#define	SBIDL_RV_2_3		0x10000000	


#define	SBIDH_RC_MASK		0x000f		
#define	SBIDH_RCE_MASK		0x7000		
#define	SBIDH_RCE_SHIFT		8
#define	SBCOREREV(sbidh) \
	((((sbidh) & SBIDH_RCE_MASK) >> SBIDH_RCE_SHIFT) | ((sbidh) & SBIDH_RC_MASK))
#define	SBIDH_CC_MASK		0x8ff0		
#define	SBIDH_CC_SHIFT		4
#define	SBIDH_VC_MASK		0xffff0000	
#define	SBIDH_VC_SHIFT		16

#define	SB_COMMIT		0xfd8		


#define	SB_VEND_BCM		0x4243		

#endif	
