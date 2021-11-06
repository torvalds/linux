/*
 * Routines to access SPROM and to parse SROM/CIS variables.
 *
 * Despite its file name, OTP contents is also parsed in this file.
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

/*
 * List of non obvious preprocessor defines used in this file and their meaning:
 * DONGLEBUILD    : building firmware that runs on the dongle's CPU
 * BCM_DONGLEVARS : NVRAM variables can be read from OTP/S(P)ROM.
 * When host may supply nvram vars in addition to the ones in OTP/SROM:
 * 	BCMHOSTVARS    		: full nic / full dongle
 * BCMDONGLEHOST  : defined when building DHD, code executes on the host in a dongle environment.
 * DHD_SPROM      : defined when building a DHD that supports reading/writing to SPROM
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <stdarg.h>
#include <bcmutils.h>
#include <hndsoc.h>
#include <sbchipc.h>
#include <bcmdevs.h>
#include <bcmendian.h>
#include <sbpcmcia.h>
#include <pcicfg.h>
#include <siutils.h>
#include <bcmsrom.h>
#include <bcmsrom_tbl.h>
#ifdef BCMSDIO
#include <bcmsdh.h>
#include <sdio.h>
#endif
#ifdef BCMSPI
#include <spid.h>
#endif

#include <bcmnvram.h>
#include <bcmotp.h>
#ifndef BCMUSBDEV_COMPOSITE
#define BCMUSBDEV_COMPOSITE
#endif
#if defined(BCMUSBDEV) || defined(BCMSDIO) || defined(BCMSDIODEV)
#include <sbsdio.h>
#include <sbhnddma.h>
#include <sbsdpcmdev.h>
#endif

#if defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG)
#include <sbsprom.h>
#endif
#include <ethernet.h>	/* for sprom content groking */

#include <sbgci.h>
#ifdef EVENT_LOG_COMPILE
#include <event_log.h>
#endif

#if defined(EVENT_LOG_COMPILE) && defined(BCMDBG_ERR) && defined(ERR_USE_EVENT_LOG)
#if defined(ERR_USE_EVENT_LOG_RA)
#define	BS_ERROR(args)	EVENT_LOG_RA(EVENT_LOG_TAG_BSROM_ERROR, args)
#else
#define	BS_ERROR(args)	EVENT_LOG_COMPACT_CAST_PAREN_ARGS(EVENT_LOG_TAG_BSROM_ERROR, args)
#endif /* ERR_USE_EVENT_LOG_RA */
#elif defined(BCMDBG_ERR) || defined(WLTEST)
#define BS_ERROR(args)	printf args
#else
#define BS_ERROR(args)
#endif	/* defined(BCMDBG_ERR) && defined(ERR_USE_EVENT_LOG) */

#if defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL)
static bool BCMATTACHDATA(is_caldata_prsnt) = FALSE;
static uint16 BCMATTACHDATA(caldata_array)[SROM_MAX / 2];
static uint8 BCMATTACHDATA(srom_sromrev);
#endif

static const char BCMATTACHDATA(rstr_uuidstr)[] =
	"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X";
static const char BCMATTACHDATA(rstr_paddr)[] = "pa%d=0x%%x";
static const char BCMATTACHDATA(rstr_pdata)[] = "pd%d=0x%%x";
static const char BCMATTACHDATA(rstr_pdatah)[] = "pdh%d=0x%%x";
static const char BCMATTACHDATA(rstr_pdatal)[] = "pdl%d=0x%%x";
static const char BCMATTACHDATA(rstr_gci_ccreg_entry)[] = "gcr%d=0x%%x";
static const char BCMATTACHDATA(rstr_hex)[] = "0x%x";

/** curmap: contains host start address of PCI BAR0 window */
static volatile uint8* srom_offset(si_t *sih, volatile void *curmap)
{
	if (sih->ccrev <= 31)
		return (volatile uint8*)curmap + PCI_BAR0_SPROM_OFFSET;
	if ((sih->cccaps & CC_CAP_SROM) == 0)
		return NULL;

	if (BUSTYPE(sih->bustype) == SI_BUS)
		return (uint8 *)((uintptr)SI_ENUM_BASE(sih) + CC_SROM_OTP);

	return (volatile uint8*)curmap + PCI_16KB0_CCREGS_OFFSET + CC_SROM_OTP;
}

#if defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG)
#define WRITE_ENABLE_DELAY	500	/* 500 ms after write enable/disable toggle */
#define WRITE_WORD_DELAY	20	/* 20 ms between each word write */
#endif

srom_info_t *sromh = NULL;

extern char *_vars;
extern uint _varsz;
#ifdef DONGLEBUILD
char * BCMATTACHDATA(_vars_otp) = NULL;
#define DONGLE_STORE_VARS_OTP_PTR(v)	(_vars_otp = (v))
#else
#define DONGLE_STORE_VARS_OTP_PTR(v)
#endif

#define SROM_CIS_SINGLE	1

#if !defined(BCMDONGLEHOST)
static int initvars_srom_si(si_t *sih, osl_t *osh, volatile void *curmap, char **vars, uint *count);
static void _initvars_srom_pci(uint8 sromrev, uint16 *srom, uint off, varbuf_t *b);
static int initvars_srom_pci(si_t *sih, volatile void *curmap, char **vars, uint *count);
static int initvars_cis_pci(si_t *sih, osl_t *osh, volatile void *curmap, char **vars, uint *count);
#endif /* !defined(BCMDONGLEHOST) */
#if !defined(BCMUSBDEV_ENABLED) && !defined(BCMSDIODEV_ENABLED) &&\
	!defined(BCMDONGLEHOST) && !defined(BCMPCIEDEV_ENABLED)
static int initvars_flash_si(si_t *sih, char **vars, uint *count);
#endif /* !defined(BCMUSBDEV) && !defined(BCMSDIODEV) && !defined(BCMDONGLEHOST) */
#ifdef BCMSDIO
#if !defined(BCMDONGLEHOST)
static int initvars_cis_sdio(si_t *sih, osl_t *osh, char **vars, uint *count);
#endif /* !defined(BCMDONGLEHOST) */
static int sprom_cmd_sdio(osl_t *osh, uint8 cmd);
static int sprom_read_sdio(osl_t *osh, uint16 addr, uint16 *data);
#if defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG)
static int sprom_write_sdio(osl_t *osh, uint16 addr, uint16 data);
#endif /* defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG) */
#endif /* BCMSDIO */
#if !defined(BCMDONGLEHOST)
#ifdef BCMSPI
static int initvars_cis_spi(si_t *sih, osl_t *osh, char **vars, uint *count);
#endif /* BCMSPI */
#endif /* !defined(BCMDONGLEHOST) */
static int sprom_read_pci(osl_t *osh, si_t *sih, volatile uint16 *sprom, uint wordoff, uint16 *buf,
                          uint nwords, bool check_crc);
#if !defined(BCMDONGLEHOST)
#if defined(BCMNVRAMW) || defined(BCMNVRAMR)
static int otp_read_pci(osl_t *osh, si_t *sih, uint16 *buf, uint bufsz);
#endif /* defined(BCMNVRAMW) || defined(BCMNVRAMR) */
#endif /* !defined(BCMDONGLEHOST) */
static uint16 srom_cc_cmd(si_t *sih, osl_t *osh, volatile void *ccregs, uint32 cmd, uint wordoff,
                          uint16 data);

#if !defined(BCMDONGLEHOST)
static int initvars_flash(si_t *sih, osl_t *osh, char **vp, uint len);
int dbushost_initvars_flash(si_t *sih, osl_t *osh, char **base, uint len);
static uint get_max_cis_size(si_t *sih);
#endif /* !defined(BCMDONGLEHOST) */

#if defined (BCMHOSTVARS)
/* Also used by wl_readconfigdata for vars download */
char BCMATTACHDATA(mfgsromvars)[VARS_MAX];
int BCMATTACHDATA(defvarslen) = 0;
#endif /* defined(BCMHOSTVARS) */

#if !defined(BCMDONGLEHOST)
#if defined (BCMHOSTVARS)
/* FIXME: Fake 4331 SROM to boot 4331 driver on QT w/o SPROM/OTP */
static char BCMATTACHDATA(defaultsromvars_4331)[] =
	"sromrev=9\0"
	"boardrev=0x1104\0"
	"boardflags=0x200\0"
	"boardflags2=0x0\0"
	"boardtype=0x524\0"
	"boardvendor=0x14e4\0"
	"boardnum=0x2064\0"
	"macaddr=00:90:4c:1a:20:64\0"
	"ccode=0x0\0"
	"regrev=0x0\0"
	"opo=0x0\0"
	"aa2g=0x7\0"
	"aa5g=0x7\0"
	"ag0=0x2\0"
	"ag1=0x2\0"
	"ag2=0x2\0"
	"ag3=0xff\0"
	"pa0b0=0xfe7f\0"
	"pa0b1=0x15d9\0"
	"pa0b2=0xfac6\0"
	"pa0itssit=0x20\0"
	"pa0maxpwr=0x48\0"
	"pa1b0=0xfe89\0"
	"pa1b1=0x14b1\0"
	"pa1b2=0xfada\0"
	"pa1lob0=0xffff\0"
	"pa1lob1=0xffff\0"
	"pa1lob2=0xffff\0"
	"pa1hib0=0xfe8f\0"
	"pa1hib1=0x13df\0"
	"pa1hib2=0xfafa\0"
	"pa1itssit=0x3e\0"
	"pa1maxpwr=0x3c\0"
	"pa1lomaxpwr=0x3c\0"
	"pa1himaxpwr=0x3c\0"
	"bxa2g=0x3\0"
	"rssisav2g=0x7\0"
	"rssismc2g=0xf\0"
	"rssismf2g=0xf\0"
	"bxa5g=0x3\0"
	"rssisav5g=0x7\0"
	"rssismc5g=0xf\0"
	"rssismf5g=0xf\0"
	"tri2g=0xff\0"
	"tri5g=0xff\0"
	"tri5gl=0xff\0"
	"tri5gh=0xff\0"
	"rxpo2g=0xff\0"
	"rxpo5g=0xff\0"
	"txchain=0x7\0"
	"rxchain=0x7\0"
	"antswitch=0x0\0"
	"tssipos2g=0x1\0"
	"extpagain2g=0x2\0"
	"pdetrange2g=0x4\0"
	"triso2g=0x3\0"
	"antswctl2g=0x0\0"
	"tssipos5g=0x1\0"
	"elna2g=0xff\0"
	"extpagain5g=0x2\0"
	"pdetrange5g=0x4\0"
	"triso5g=0x3\0"
	"antswctl5g=0x0\0"
	"elna5g=0xff\0"
	"cckbw202gpo=0x0\0"
	"cckbw20ul2gpo=0x0\0"
	"legofdmbw202gpo=0x0\0"
	"legofdmbw20ul2gpo=0x0\0"
	"legofdmbw205glpo=0x0\0"
	"legofdmbw20ul5glpo=0x0\0"
	"legofdmbw205gmpo=0x0\0"
	"legofdmbw20ul5gmpo=0x0\0"
	"legofdmbw205ghpo=0x0\0"
	"legofdmbw20ul5ghpo=0x0\0"
	"mcsbw202gpo=0x0\0"
	"mcsbw20ul2gpo=0x0\0"
	"mcsbw402gpo=0x0\0"
	"mcsbw205glpo=0x0\0"
	"mcsbw20ul5glpo=0x0\0"
	"mcsbw405glpo=0x0\0"
	"mcsbw205gmpo=0x0\0"
	"mcsbw20ul5gmpo=0x0\0"
	"mcsbw405gmpo=0x0\0"
	"mcsbw205ghpo=0x0\0"
	"mcsbw20ul5ghpo=0x0\0"
	"mcsbw405ghpo=0x0\0"
	"mcs32po=0x0\0"
	"legofdm40duppo=0x0\0"
	"maxp2ga0=0x48\0"
	"itt2ga0=0x20\0"
	"itt5ga0=0x3e\0"
	"pa2gw0a0=0xfe7f\0"
	"pa2gw1a0=0x15d9\0"
	"pa2gw2a0=0xfac6\0"
	"maxp5ga0=0x3c\0"
	"maxp5gha0=0x3c\0"
	"maxp5gla0=0x3c\0"
	"pa5gw0a0=0xfe89\0"
	"pa5gw1a0=0x14b1\0"
	"pa5gw2a0=0xfada\0"
	"pa5glw0a0=0xffff\0"
	"pa5glw1a0=0xffff\0"
	"pa5glw2a0=0xffff\0"
	"pa5ghw0a0=0xfe8f\0"
	"pa5ghw1a0=0x13df\0"
	"pa5ghw2a0=0xfafa\0"
	"maxp2ga1=0x48\0"
	"itt2ga1=0x20\0"
	"itt5ga1=0x3e\0"
	"pa2gw0a1=0xfe54\0"
	"pa2gw1a1=0x1563\0"
	"pa2gw2a1=0xfa7f\0"
	"maxp5ga1=0x3c\0"
	"maxp5gha1=0x3c\0"
	"maxp5gla1=0x3c\0"
	"pa5gw0a1=0xfe53\0"
	"pa5gw1a1=0x14fe\0"
	"pa5gw2a1=0xfa94\0"
	"pa5glw0a1=0xffff\0"
	"pa5glw1a1=0xffff\0"
	"pa5glw2a1=0xffff\0"
	"pa5ghw0a1=0xfe6e\0"
	"pa5ghw1a1=0x1457\0"
	"pa5ghw2a1=0xfab9\0"
	"END\0";

static char BCMATTACHDATA(defaultsromvars_4360)[] =
	"sromrev=11\0"
	"boardrev=0x1421\0"
	"boardflags=0x10401001\0"
	"boardflags2=0x0\0"
	"boardtype=0x61b\0"
	"subvid=0x14e4\0"
	"boardflags3=0x1\0"
	"boardnum=62526\0"
	"macaddr=00:90:4c:0d:f4:3e\0"
	"ccode=X0\0"
	"regrev=15\0"
	"aa2g=7\0"
	"aa5g=7\0"
	"agbg0=71\0"
	"agbg1=71\0"
	"agbg2=133\0"
	"aga0=71\0"
	"aga1=133\0"
	"aga2=133\0"
	"antswitch=0\0"
	"tssiposslope2g=1\0"
	"epagain2g=0\0"
	"pdgain2g=9\0"
	"tworangetssi2g=0\0"
	"papdcap2g=0\0"
	"femctrl=2\0"
	"tssiposslope5g=1\0"
	"epagain5g=0\0"
	"pdgain5g=9\0"
	"tworangetssi5g=0\0"
	"papdcap5g=0\0"
	"gainctrlsph=0\0"
	"tempthresh=255\0"
	"tempoffset=255\0"
	"rawtempsense=0x1ff\0"
	"measpower=0x7f\0"
	"tempsense_slope=0xff\0"
	"tempcorrx=0x3f\0"
	"tempsense_option=0x3\0"
	"xtalfreq=65535\0"
	"phycal_tempdelta=255\0"
	"temps_period=15\0"
	"temps_hysteresis=15\0"
	"measpower1=0x7f\0"
	"measpower2=0x7f\0"
	"pdoffset2g40ma0=15\0"
	"pdoffset2g40ma1=15\0"
	"pdoffset2g40ma2=15\0"
	"pdoffset2g40mvalid=1\0"
	"pdoffset40ma0=9010\0"
	"pdoffset40ma1=12834\0"
	"pdoffset40ma2=8994\0"
	"pdoffset80ma0=16\0"
	"pdoffset80ma1=4096\0"
	"pdoffset80ma2=0\0"
	"subband5gver=0x4\0"
	"cckbw202gpo=0\0"
	"cckbw20ul2gpo=0\0"
	"mcsbw202gpo=2571386880\0"
	"mcsbw402gpo=2571386880\0"
	"dot11agofdmhrbw202gpo=17408\0"
	"ofdmlrbw202gpo=0\0"
	"mcsbw205glpo=4001923072\0"
	"mcsbw405glpo=4001923072\0"
	"mcsbw805glpo=4001923072\0"
	"mcsbw1605glpo=0\0"
	"mcsbw205gmpo=3431497728\0"
	"mcsbw405gmpo=3431497728\0"
	"mcsbw805gmpo=3431497728\0"
	"mcsbw1605gmpo=0\0"
	"mcsbw205ghpo=3431497728\0"
	"mcsbw405ghpo=3431497728\0"
	"mcsbw805ghpo=3431497728\0"
	"mcsbw1605ghpo=0\0"
	"mcslr5glpo=0\0"
	"mcslr5gmpo=0\0"
	"mcslr5ghpo=0\0"
	"sb20in40hrpo=0\0"
	"sb20in80and160hr5glpo=0\0"
	"sb40and80hr5glpo=0\0"
	"sb20in80and160hr5gmpo=0\0"
	"sb40and80hr5gmpo=0\0"
	"sb20in80and160hr5ghpo=0\0"
	"sb40and80hr5ghpo=0\0"
	"sb20in40lrpo=0\0"
	"sb20in80and160lr5glpo=0\0"
	"sb40and80lr5glpo=0\0"
	"sb20in80and160lr5gmpo=0\0"
	"sb40and80lr5gmpo=0\0"
	"sb20in80and160lr5ghpo=0\0"
	"sb40and80lr5ghpo=0\0"
	"dot11agduphrpo=0\0"
	"dot11agduplrpo=0\0"
	"pcieingress_war=15\0"
	"sar2g=18\0"
	"sar5g=15\0"
	"noiselvl2ga0=31\0"
	"noiselvl2ga1=31\0"
	"noiselvl2ga2=31\0"
	"noiselvl5ga0=31,31,31,31\0"
	"noiselvl5ga1=31,31,31,31\0"
	"noiselvl5ga2=31,31,31,31\0"
	"rxgainerr2ga0=63\0"
	"rxgainerr2ga1=31\0"
	"rxgainerr2ga2=31\0"
	"rxgainerr5ga0=63,63,63,63\0"
	"rxgainerr5ga1=31,31,31,31\0"
	"rxgainerr5ga2=31,31,31,31\0"
	"maxp2ga0=76\0"
	"pa2ga0=0xff3c,0x172c,0xfd20\0"
	"rxgains5gmelnagaina0=7\0"
	"rxgains5gmtrisoa0=15\0"
	"rxgains5gmtrelnabypa0=1\0"
	"rxgains5ghelnagaina0=7\0"
	"rxgains5ghtrisoa0=15\0"
	"rxgains5ghtrelnabypa0=1\0"
	"rxgains2gelnagaina0=4\0"
	"rxgains2gtrisoa0=7\0"
	"rxgains2gtrelnabypa0=1\0"
	"rxgains5gelnagaina0=3\0"
	"rxgains5gtrisoa0=7\0"
	"rxgains5gtrelnabypa0=1\0"
	"maxp5ga0=76,76,76,76\0"
"pa5ga0=0xff3a,0x14d4,0xfd5f,0xff36,0x1626,0xfd2e,0xff42,0x15bd,0xfd47,0xff39,0x15a3,0xfd3d\0"
	"maxp2ga1=76\0"
	"pa2ga1=0xff2a,0x16b2,0xfd28\0"
	"rxgains5gmelnagaina1=7\0"
	"rxgains5gmtrisoa1=15\0"
	"rxgains5gmtrelnabypa1=1\0"
	"rxgains5ghelnagaina1=7\0"
	"rxgains5ghtrisoa1=15\0"
	"rxgains5ghtrelnabypa1=1\0"
	"rxgains2gelnagaina1=3\0"
	"rxgains2gtrisoa1=6\0"
	"rxgains2gtrelnabypa1=1\0"
	"rxgains5gelnagaina1=3\0"
	"rxgains5gtrisoa1=6\0"
	"rxgains5gtrelnabypa1=1\0"
	"maxp5ga1=76,76,76,76\0"
"pa5ga1=0xff4e,0x1530,0xfd53,0xff58,0x15b4,0xfd4d,0xff58,0x1671,0xfd2f,0xff55,0x15e2,0xfd46\0"
	"maxp2ga2=76\0"
	"pa2ga2=0xff3c,0x1736,0xfd1f\0"
	"rxgains5gmelnagaina2=7\0"
	"rxgains5gmtrisoa2=15\0"
	"rxgains5gmtrelnabypa2=1\0"
	"rxgains5ghelnagaina2=7\0"
	"rxgains5ghtrisoa2=15\0"
	"rxgains5ghtrelnabypa2=1\0"
	"rxgains2gelnagaina2=4\0"
	"rxgains2gtrisoa2=7\0"
	"rxgains2gtrelnabypa2=1\0"
	"rxgains5gelnagaina2=3\0"
	"rxgains5gtrisoa2=7\0"
	"rxgains5gtrelnabypa2=1\0"
	"maxp5ga2=76,76,76,76\0"
"pa5ga2=0xff2d,0x144a,0xfd63,0xff35,0x15d7,0xfd3b,0xff35,0x1668,0xfd2f,0xff31,0x1664,0xfd27\0"
	"END\0";

#endif /* defined(BCMHOSTVARS) */
#endif /* !defined(BCMDONGLEHOST) */

#if !defined(BCMDONGLEHOST)
#if defined(BCMHOSTVARS)
static char BCMATTACHDATA(defaultsromvars_wltest)[] =
	"macaddr=00:90:4c:f8:00:01\0"
	"et0macaddr=00:11:22:33:44:52\0"
	"et0phyaddr=30\0"
	"et0mdcport=0\0"
	"gpio2=robo_reset\0"
	"boardvendor=0x14e4\0"
	"boardflags=0x210\0"
	"boardflags2=0\0"
	"boardtype=0x04c3\0"
	"boardrev=0x1100\0"
	"sromrev=8\0"
	"devid=0x432c\0"
	"ccode=0\0"
	"regrev=0\0"
	"aa2g=3\0"
	"ag0=2\0"
	"ag1=2\0"
	"aa5g=3\0"
	"aa0=2\0"
	"aa1=2\0"
	"txchain=3\0"
	"rxchain=3\0"
	"antswitch=0\0"
	"itt2ga0=0x20\0"
	"maxp2ga0=0x48\0"
	"pa2gw0a0=0xfe9e\0"
	"pa2gw1a0=0x15d5\0"
	"pa2gw2a0=0xfae9\0"
	"itt2ga1=0x20\0"
	"maxp2ga1=0x48\0"
	"pa2gw0a1=0xfeb3\0"
	"pa2gw1a1=0x15c9\0"
	"pa2gw2a1=0xfaf7\0"
	"tssipos2g=1\0"
	"extpagain2g=0\0"
	"pdetrange2g=0\0"
	"triso2g=3\0"
	"antswctl2g=0\0"
	"tssipos5g=1\0"
	"extpagain5g=0\0"
	"pdetrange5g=0\0"
	"triso5g=3\0"
	"antswctl5g=0\0"
	"cck2gpo=0\0"
	"ofdm2gpo=0\0"
	"mcs2gpo0=0\0"
	"mcs2gpo1=0\0"
	"mcs2gpo2=0\0"
	"mcs2gpo3=0\0"
	"mcs2gpo4=0\0"
	"mcs2gpo5=0\0"
	"mcs2gpo6=0\0"
	"mcs2gpo7=0\0"
	"cddpo=0\0"
	"stbcpo=0\0"
	"bw40po=4\0"
	"bwduppo=0\0"
	"END\0";

/**
 * The contents of this array is a first attempt, is likely incorrect for 43602, needs to be
 * edited in a later stage.
 */
static char BCMATTACHDATA(defaultsromvars_43602)[] =
	"sromrev=11\0"
	"boardrev=0x1421\0"
	"boardflags=0x10401001\0"
	"boardflags2=0x00000002\0"
	"boardflags3=0x00000003\0"
	"boardtype=0x61b\0"
	"subvid=0x14e4\0"
	"boardnum=62526\0"
	"macaddr=00:90:4c:0d:f4:3e\0"
	"ccode=X0\0"
	"regrev=15\0"
	"aa2g=7\0"
	"aa5g=7\0"
	"agbg0=71\0"
	"agbg1=71\0"
	"agbg2=133\0"
	"aga0=71\0"
	"aga1=133\0"
	"aga2=133\0"
	"antswitch=0\0"
	"tssiposslope2g=1\0"
	"epagain2g=0\0"
	"pdgain2g=9\0"
	"tworangetssi2g=0\0"
	"papdcap2g=0\0"
	"femctrl=2\0"
	"tssiposslope5g=1\0"
	"epagain5g=0\0"
	"pdgain5g=9\0"
	"tworangetssi5g=0\0"
	"papdcap5g=0\0"
	"gainctrlsph=0\0"
	"tempthresh=255\0"
	"tempoffset=255\0"
	"rawtempsense=0x1ff\0"
	"measpower=0x7f\0"
	"tempsense_slope=0xff\0"
	"tempcorrx=0x3f\0"
	"tempsense_option=0x3\0"
	"xtalfreq=40000\0"
	"phycal_tempdelta=255\0"
	"temps_period=15\0"
	"temps_hysteresis=15\0"
	"measpower1=0x7f\0"
	"measpower2=0x7f\0"
	"pdoffset2g40ma0=15\0"
	"pdoffset2g40ma1=15\0"
	"pdoffset2g40ma2=15\0"
	"pdoffset2g40mvalid=1\0"
	"pdoffset40ma0=9010\0"
	"pdoffset40ma1=12834\0"
	"pdoffset40ma2=8994\0"
	"pdoffset80ma0=16\0"
	"pdoffset80ma1=4096\0"
	"pdoffset80ma2=0\0"
	"subband5gver=0x4\0"
	"cckbw202gpo=0\0"
	"cckbw20ul2gpo=0\0"
	"mcsbw202gpo=2571386880\0"
	"mcsbw402gpo=2571386880\0"
	"dot11agofdmhrbw202gpo=17408\0"
	"ofdmlrbw202gpo=0\0"
	"mcsbw205glpo=4001923072\0"
	"mcsbw405glpo=4001923072\0"
	"mcsbw805glpo=4001923072\0"
	"mcsbw1605glpo=0\0"
	"mcsbw205gmpo=3431497728\0"
	"mcsbw405gmpo=3431497728\0"
	"mcsbw805gmpo=3431497728\0"
	"mcsbw1605gmpo=0\0"
	"mcsbw205ghpo=3431497728\0"
	"mcsbw405ghpo=3431497728\0"
	"mcsbw805ghpo=3431497728\0"
	"mcsbw1605ghpo=0\0"
	"mcslr5glpo=0\0"
	"mcslr5gmpo=0\0"
	"mcslr5ghpo=0\0"
	"sb20in40hrpo=0\0"
	"sb20in80and160hr5glpo=0\0"
	"sb40and80hr5glpo=0\0"
	"sb20in80and160hr5gmpo=0\0"
	"sb40and80hr5gmpo=0\0"
	"sb20in80and160hr5ghpo=0\0"
	"sb40and80hr5ghpo=0\0"
	"sb20in40lrpo=0\0"
	"sb20in80and160lr5glpo=0\0"
	"sb40and80lr5glpo=0\0"
	"sb20in80and160lr5gmpo=0\0"
	"sb40and80lr5gmpo=0\0"
	"sb20in80and160lr5ghpo=0\0"
	"sb40and80lr5ghpo=0\0"
	"dot11agduphrpo=0\0"
	"dot11agduplrpo=0\0"
	"pcieingress_war=15\0"
	"sar2g=18\0"
	"sar5g=15\0"
	"noiselvl2ga0=31\0"
	"noiselvl2ga1=31\0"
	"noiselvl2ga2=31\0"
	"noiselvl5ga0=31,31,31,31\0"
	"noiselvl5ga1=31,31,31,31\0"
	"noiselvl5ga2=31,31,31,31\0"
	"rxgainerr2ga0=63\0"
	"rxgainerr2ga1=31\0"
	"rxgainerr2ga2=31\0"
	"rxgainerr5ga0=63,63,63,63\0"
	"rxgainerr5ga1=31,31,31,31\0"
	"rxgainerr5ga2=31,31,31,31\0"
	"maxp2ga0=76\0"
	"pa2ga0=0xff3c,0x172c,0xfd20\0"
	"rxgains5gmelnagaina0=7\0"
	"rxgains5gmtrisoa0=15\0"
	"rxgains5gmtrelnabypa0=1\0"
	"rxgains5ghelnagaina0=7\0"
	"rxgains5ghtrisoa0=15\0"
	"rxgains5ghtrelnabypa0=1\0"
	"rxgains2gelnagaina0=4\0"
	"rxgains2gtrisoa0=7\0"
	"rxgains2gtrelnabypa0=1\0"
	"rxgains5gelnagaina0=3\0"
	"rxgains5gtrisoa0=7\0"
	"rxgains5gtrelnabypa0=1\0"
	"maxp5ga0=76,76,76,76\0"
"pa5ga0=0xff3a,0x14d4,0xfd5f,0xff36,0x1626,0xfd2e,0xff42,0x15bd,0xfd47,0xff39,0x15a3,0xfd3d\0"
	"maxp2ga1=76\0"
	"pa2ga1=0xff2a,0x16b2,0xfd28\0"
	"rxgains5gmelnagaina1=7\0"
	"rxgains5gmtrisoa1=15\0"
	"rxgains5gmtrelnabypa1=1\0"
	"rxgains5ghelnagaina1=7\0"
	"rxgains5ghtrisoa1=15\0"
	"rxgains5ghtrelnabypa1=1\0"
	"rxgains2gelnagaina1=3\0"
	"rxgains2gtrisoa1=6\0"
	"rxgains2gtrelnabypa1=1\0"
	"rxgains5gelnagaina1=3\0"
	"rxgains5gtrisoa1=6\0"
	"rxgains5gtrelnabypa1=1\0"
	"maxp5ga1=76,76,76,76\0"
"pa5ga1=0xff4e,0x1530,0xfd53,0xff58,0x15b4,0xfd4d,0xff58,0x1671,0xfd2f,0xff55,0x15e2,0xfd46\0"
	"maxp2ga2=76\0"
	"pa2ga2=0xff3c,0x1736,0xfd1f\0"
	"rxgains5gmelnagaina2=7\0"
	"rxgains5gmtrisoa2=15\0"
	"rxgains5gmtrelnabypa2=1\0"
	"rxgains5ghelnagaina2=7\0"
	"rxgains5ghtrisoa2=15\0"
	"rxgains5ghtrelnabypa2=1\0"
	"rxgains2gelnagaina2=4\0"
	"rxgains2gtrisoa2=7\0"
	"rxgains2gtrelnabypa2=1\0"
	"rxgains5gelnagaina2=3\0"
	"rxgains5gtrisoa2=7\0"
	"rxgains5gtrelnabypa2=1\0"
	"maxp5ga2=76,76,76,76\0"
"pa5ga2=0xff2d,0x144a,0xfd63,0xff35,0x15d7,0xfd3b,0xff35,0x1668,0xfd2f,0xff31,0x1664,0xfd27\0"
	"END\0";

/**
 * The contents of this array is a first attempt, was copied from 4378, needs to be edited in
 * a later stage.
 */
static char BCMATTACHDATA(defaultsromvars_4378)[] =
	"cckdigfilttype=4\0"
	"sromrev=11\0"
	"boardrev=0x1102\0"
	"boardtype=0x0771\0"
	"boardflags=0x10481201\0"
	"boardflags2=0x00000000\0"
	"boardflags3=0x04000080\0"
	"macaddr=00:90:4c:12:43:47\0"
	"ccode=0\0"
	"regrev=0\0"
	"antswitch=0\0"
	"pdgain5g=0\0"
	"pdgain2g=0\0"
	"tworangetssi2g=0\0"
	"tworangetssi5g=0\0"
	"femctrl=16\0"
	"vendid=0x14e4\0"
	"devid=0x4425\0"
	"manfid=0x2d0\0"
	"nocrc=1\0"
	"btc_params82=0x1a0\0"
	"otpimagesize=502\0"
	"xtalfreq=37400\0"
	"rxgains2gelnagaina0=3\0"
	"rxgains2gtrisoa0=7\0"
	"rxgains2gtrelnabypa0=1\0"
	"rxgains5gelnagaina0=3\0"
	"rxgains5gtrisoa0=6\0"
	"rxgains5gtrelnabypa0=1\0"
	"rxgains5gmelnagaina0=3\0"
	"rxgains5gmtrisoa0=6\0"
	"rxgains5gmtrelnabypa0=1\0"
	"rxgains5ghelnagaina0=3\0"
	"rxgains5ghtrisoa0=6\0"
	"rxgains5ghtrelnabypa0=1\0"
	"rxgains2gelnagaina1=3\0"
	"rxgains2gtrisoa1=7\0"
	"rxgains2gtrelnabypa1=1\0"
	"rxgains5gelnagaina1=3\0"
	"rxgains5gtrisoa1=6\0"
	"rxgains5gtrelnabypa1=1\0"
	"rxgains5gmelnagaina1=3\0"
	"rxgains5gmtrisoa1=6\0"
	"rxgains5gmtrelnabypa1=1\0"
	"rxgains5ghelnagaina1=3\0"
	"rxgains5ghtrisoa1=6\0"
	"rxgains5ghtrelnabypa1=1\0"
	"rxchain=3\0"
	"txchain=3\0"
	"aa2g=3\0"
	"aa5g=3\0"
	"agbg0=2\0"
	"agbg1=2\0"
	"aga0=2\0"
	"aga1=2\0"
	"tssipos2g=1\0"
	"tssipos5g=1\0"
	"tempthresh=255\0"
	"tempoffset=255\0"
	"rawtempsense=0x1ff\0"
	"pa2gccka0=-200,7392,-897\0"
	"pa2gccka1=-198,7522,-907\0"
	"pa2ga0=-174,7035,-838\0"
	"pa2ga1=-185,6772,-811\0"
	"pa5ga0=-175,7296,-887,-164,7553,-910,-155,7801,-936,-149,7908,-951\0"
	"pa5ga1=-155,7675,-925,-148,7851,-940,-152,7930,-954,-143,8121,-969\0"
	"pa5gbw4080a0=-178,7872,-959,-173,8107,-986,-165,8398,-1019,-150,8809,-1063\0"
	"pa5gbw4080a1=-166,8179,-993,-161,8378,-1015,-165,8402,-1019,-155,8757,-1057\0"
	"maxp2ga0=66\0"
	"maxp2ga1=66\0"
	"maxp5ga0=66,66,66,66\0"
	"maxp5ga1=66,66,66,66\0"
	"subband5gver=0x4\0"
	"paparambwver=3\0"
	"cckpwroffset0=0\0"
	"cckpwroffset1=0\0"
	"pdoffset40ma0=0x0000\0"
	"pdoffset80ma0=0xeeee\0"
	"pdoffset40ma1=0x0000\0"
	"pdoffset80ma1=0xeeee\0"
	"cckbw202gpo=0\0"
	"cckbw20ul2gpo=0\0"
	"mcsbw202gpo=0xEC888222\0"
	"mcsbw402gpo=0xEC888222\0"
	"dot11agofdmhrbw202gpo=0x6622\0"
	"ofdmlrbw202gpo=0x0000\0"
	"mcsbw205glpo=0xCA666000\0"
	"mcsbw405glpo=0xCA666000\0"
	"mcsbw805glpo=0xEA666000\0"
	"mcsbw1605glpo=0\0"
	"mcsbw205gmpo=0xCA666000\0"
	"mcsbw405gmpo=0xCA666000\0"
	"mcsbw805gmpo=0xEA666000\0"
	"mcsbw1605gmpo=0\0"
	"mcsbw205ghpo=0xCA666000\0"
	"mcsbw405ghpo=0xCA666000\0"
	"mcsbw805ghpo=0xEA666000\0"
	"mcsbw1605ghpo=0\0"
	"mcslr5glpo=0x0000\0"
	"mcslr5gmpo=0x0000\0"
	"mcslr5ghpo=0x0000\0"
	"sb20in40hrpo=0x0\0"
	"sb20in80and160hr5glpo=0x0\0"
	"sb40and80hr5glpo=0x0\0"
	"sb20in80and160hr5gmpo=0x0\0"
	"sb40and80hr5gmpo=0x0\0"
	"sb20in80and160hr5ghpo=0x0\0"
	"sb40and80hr5ghpo=0x0\0"
	"sb20in40lrpo=0x0\0"
	"sb20in80and160lr5glpo=0x0\0"
	"sb40and80lr5glpo=0x0\0"
	"sb20in80and160lr5gmpo=0x0\0"
	"sb40and80lr5gmpo=0x0\0"
	"sb20in80and160lr5ghpo=0x0\0"
	"sb40and80lr5ghpo=0x0\0"
	"dot11agduphrpo=0x0\0"
	"dot11agduplrpo=0x0\0"
	"phycal_tempdelta=15\0"
	"temps_period=15\0"
	"temps_hysteresis=15\0"
	"swctrlmap_2g=0x00000404,0x0a0a0000,0x02020000,0x010a02,0x1fe\0"
	"swctrlmapext_2g=0x00000000,0x00000000,0x00000000,0x000000,0x000\0"
	"swctrlmap_5g=0x00001010,0x60600000,0x40400000,0x000000,0x0f0\0"
	"swctrlmapext_5g=0x00000000,0x00000000,0x00000000,0x000000,0x000\0"
	"powoffs2gtna0=1,3,3,1,0,0,1,2,2,2,1,1,0,0\0"
	"powoffs2gtna1=-1,1,1,1,0,0,1,2,3,2,2,0,0,0\0"
	"END\0";

/**
 * The contents of this array is a first attempt, was copied from 4387, needs to be edited in
 * a later stage.
 */
static char BCMATTACHDATA(defaultsromvars_4387)[] =
	"cckdigfilttype=4\0"
	"sromrev=11\0"
	"boardrev=0x1102\0"
	"boardtype=0x0771\0"
	"boardflags=0x10481201\0"
	"boardflags2=0x00000000\0"
	"boardflags3=0x04000080\0"
	"macaddr=00:90:4c:12:43:47\0"
	"ccode=0\0"
	"regrev=0\0"
	"antswitch=0\0"
	"pdgain5g=0\0"
	"pdgain2g=0\0"
	"tworangetssi2g=0\0"
	"tworangetssi5g=0\0"
	"femctrl=16\0"
	"vendid=0x14e4\0"
	"devid=0x4425\0"
	"manfid=0x2d0\0"
	"nocrc=1\0"
	"btc_params82=0x1a0\0"
	"otpimagesize=502\0"
	"xtalfreq=37400\0"
	"rxgains2gelnagaina0=3\0"
	"rxgains2gtrisoa0=7\0"
	"rxgains2gtrelnabypa0=1\0"
	"rxgains5gelnagaina0=3\0"
	"rxgains5gtrisoa0=6\0"
	"rxgains5gtrelnabypa0=1\0"
	"rxgains5gmelnagaina0=3\0"
	"rxgains5gmtrisoa0=6\0"
	"rxgains5gmtrelnabypa0=1\0"
	"rxgains5ghelnagaina0=3\0"
	"rxgains5ghtrisoa0=6\0"
	"rxgains5ghtrelnabypa0=1\0"
	"rxgains2gelnagaina1=3\0"
	"rxgains2gtrisoa1=7\0"
	"rxgains2gtrelnabypa1=1\0"
	"rxgains5gelnagaina1=3\0"
	"rxgains5gtrisoa1=6\0"
	"rxgains5gtrelnabypa1=1\0"
	"rxgains5gmelnagaina1=3\0"
	"rxgains5gmtrisoa1=6\0"
	"rxgains5gmtrelnabypa1=1\0"
	"rxgains5ghelnagaina1=3\0"
	"rxgains5ghtrisoa1=6\0"
	"rxgains5ghtrelnabypa1=1\0"
	"rxchain=3\0"
	"txchain=3\0"
	"aa2g=3\0"
	"aa5g=3\0"
	"agbg0=2\0"
	"agbg1=2\0"
	"aga0=2\0"
	"aga1=2\0"
	"tssipos2g=1\0"
	"tssipos5g=1\0"
	"tempthresh=255\0"
	"tempoffset=255\0"
	"rawtempsense=0x1ff\0"
	"pa2gccka0=-200,7392,-897\0"
	"pa2gccka1=-198,7522,-907\0"
	"pa2ga0=-174,7035,-838\0"
	"pa2ga1=-185,6772,-811\0"
	"pa5ga0=-175,7296,-887,-164,7553,-910,-155,7801,-936,-149,7908,-951\0"
	"pa5ga1=-155,7675,-925,-148,7851,-940,-152,7930,-954,-143,8121,-969\0"
	"pa5gbw4080a0=-178,7872,-959,-173,8107,-986,-165,8398,-1019,-150,8809,-1063\0"
	"pa5gbw4080a1=-166,8179,-993,-161,8378,-1015,-165,8402,-1019,-155,8757,-1057\0"
	"maxp2ga0=66\0"
	"maxp2ga1=66\0"
	"maxp5ga0=66,66,66,66\0"
	"maxp5ga1=66,66,66,66\0"
	"subband5gver=0x4\0"
	"paparambwver=3\0"
	"cckpwroffset0=0\0"
	"cckpwroffset1=0\0"
	"pdoffset40ma0=0x0000\0"
	"pdoffset80ma0=0xeeee\0"
	"pdoffset40ma1=0x0000\0"
	"pdoffset80ma1=0xeeee\0"
	"cckbw202gpo=0\0"
	"cckbw20ul2gpo=0\0"
	"mcsbw202gpo=0xEC888222\0"
	"mcsbw402gpo=0xEC888222\0"
	"dot11agofdmhrbw202gpo=0x6622\0"
	"ofdmlrbw202gpo=0x0000\0"
	"mcsbw205glpo=0xCA666000\0"
	"mcsbw405glpo=0xCA666000\0"
	"mcsbw805glpo=0xEA666000\0"
	"mcsbw1605glpo=0\0"
	"mcsbw205gmpo=0xCA666000\0"
	"mcsbw405gmpo=0xCA666000\0"
	"mcsbw805gmpo=0xEA666000\0"
	"mcsbw1605gmpo=0\0"
	"mcsbw205ghpo=0xCA666000\0"
	"mcsbw405ghpo=0xCA666000\0"
	"mcsbw805ghpo=0xEA666000\0"
	"mcsbw1605ghpo=0\0"
	"mcslr5glpo=0x0000\0"
	"mcslr5gmpo=0x0000\0"
	"mcslr5ghpo=0x0000\0"
	"sb20in40hrpo=0x0\0"
	"sb20in80and160hr5glpo=0x0\0"
	"sb40and80hr5glpo=0x0\0"
	"sb20in80and160hr5gmpo=0x0\0"
	"sb40and80hr5gmpo=0x0\0"
	"sb20in80and160hr5ghpo=0x0\0"
	"sb40and80hr5ghpo=0x0\0"
	"sb20in40lrpo=0x0\0"
	"sb20in80and160lr5glpo=0x0\0"
	"sb40and80lr5glpo=0x0\0"
	"sb20in80and160lr5gmpo=0x0\0"
	"sb40and80lr5gmpo=0x0\0"
	"sb20in80and160lr5ghpo=0x0\0"
	"sb40and80lr5ghpo=0x0\0"
	"dot11agduphrpo=0x0\0"
	"dot11agduplrpo=0x0\0"
	"phycal_tempdelta=15\0"
	"temps_period=15\0"
	"temps_hysteresis=15\0"
	"swctrlmap_2g=0x00000404,0x0a0a0000,0x02020000,0x010a02,0x1fe\0"
	"swctrlmapext_2g=0x00000000,0x00000000,0x00000000,0x000000,0x000\0"
	"swctrlmap_5g=0x00001010,0x60600000,0x40400000,0x000000,0x0f0\0"
	"swctrlmapext_5g=0x00000000,0x00000000,0x00000000,0x000000,0x000\0"
	"powoffs2gtna0=1,3,3,1,0,0,1,2,2,2,1,1,0,0\0"
	"powoffs2gtna1=-1,1,1,1,0,0,1,2,3,2,2,0,0,0\0"
	"END\0";

#endif /* defined(BCMHOSTVARS) */
#endif /* !defined(BCMDONGLEHOST) */

static bool srvars_inited = FALSE; /* Use OTP/SROM as global variables */

#if (!defined(BCMDONGLEHOST) && defined(BCMHOSTVARS))
/* It must end with pattern of "END" */
static uint
BCMATTACHFN(srom_vars_len)(char *vars)
{
	uint pos = 0;
	uint len;
	char *s;
	char *emark = "END";
	uint emark_len = strlen(emark) + 1;

	for (s = vars; s && *s;) {

		if (strcmp(s, emark) == 0)
			break;

		len = strlen(s);
		s += strlen(s) + 1;
		pos += len + 1;
		/* BS_ERROR(("len %d vars[pos] %s\n", pos, s)); */
		if (pos >= (VARS_MAX - emark_len)) {
			return 0;
		}
	}

	return pos + emark_len;	/* include the "END\0" */
}
#endif /* BCMHOSTVARS */

#if !defined(BCMDONGLEHOST)
#ifdef BCMNVRAMVARS
static int
BCMATTACHFN(initvars_nvram_vars)(si_t *sih, osl_t *osh, char **vars, uint *vars_sz)
{
	int ret;

	ASSERT(vars != NULL && vars_sz != NULL);

	/* allocate maximum buffer as we don't know big it should be */
	*vars = MALLOC(osh, MAXSZ_NVRAM_VARS);
	if (*vars == NULL) {
		ret = BCME_NOMEM;
		goto fail;
	}
	*vars_sz = MAXSZ_NVRAM_VARS;

	/* query the name=value pairs */
	if ((ret = nvram_getall(*vars, *vars_sz)) != BCME_OK) {
		goto fail;
	}

	/* treat empty name=value list as an error so that we can indicate
	 * the condition up throught error code return...
	 */
	if (*vars_sz == 0) {
		ret = BCME_ERROR;
		goto fail;
	}

	return BCME_OK;

fail:
	if (*vars != NULL) {
		MFREE(osh, *vars, MAXSZ_NVRAM_VARS);
	}
	*vars = NULL;
	*vars_sz = 0;
	return ret;
}
#endif /* BCMNVRAMVARS */

/**
 * Initialize local vars from the right source for this platform. Called from siutils.c.
 *
 * vars - pointer to a to-be created pointer area for "environment" variables. Some callers of this
 *        function set 'vars' to NULL, in that case this function will prematurely return.
 *
 * Return 0 on success, nonzero on error.
 */
int
BCMATTACHFN(srom_var_init)(si_t *sih, uint bustype, volatile void *curmap, osl_t *osh,
	char **vars, uint *count)
{
	ASSERT(bustype == BUSTYPE(bustype));
	if (vars == NULL || count == NULL)
		return (0);

	*vars = NULL;
	*count = 0;

	switch (BUSTYPE(bustype)) {
	case SI_BUS:
#ifdef BCMPCIEDEV
		if (BCMPCIEDEV_ENAB()) {
			int ret;

			ret = initvars_cis_pci(sih, osh, curmap, vars, count);

#ifdef BCMPCIEDEV_SROM_FORMAT
			if (ret)
				ret = initvars_srom_pci(sih, curmap, vars, count);
#endif
			if (ret)
				ret =  initvars_srom_si(sih, osh, curmap, vars, count);
			return ret;
		} else
#endif /* BCMPCIEDEV */
		{
			return initvars_srom_si(sih, osh, curmap, vars, count);
		}
	case PCI_BUS: {
		int ret;

#ifdef BCMNVRAMVARS
		if ((ret = initvars_nvram_vars(sih, osh, vars, count)) == BCME_OK) {
			return ret;
		} else
#endif
		{
			ASSERT(curmap != NULL);
			if (curmap == NULL)
				return (-1);

			/* First check for CIS format. if not CIS, try SROM format */
			if ((ret = initvars_cis_pci(sih, osh, curmap, vars, count)))
				return initvars_srom_pci(sih, curmap, vars, count);
			return ret;
		}
	}

#ifdef BCMSDIO
	case SDIO_BUS:
		return initvars_cis_sdio(sih, osh, vars, count);
#endif /* BCMSDIO */

#ifdef BCMSPI
	case SPI_BUS:
		return initvars_cis_spi(sih, osh, vars, count);
#endif /* BCMSPI */

	default:
		ASSERT(0);
	}
	return (-1);
}
#endif /* !defined(BCMDONGLEHOST) */

/** support only 16-bit word read from srom */
int
srom_read(si_t *sih, uint bustype, volatile void *curmap, osl_t *osh,
          uint byteoff, uint nbytes, uint16 *buf, bool check_crc)
{
	uint i, off, nw;

	BCM_REFERENCE(i);

	ASSERT(bustype == BUSTYPE(bustype));

	/* check input - 16-bit access only */
	if (byteoff & 1 || nbytes & 1 || (byteoff + nbytes) > SROM_MAX)
		return 1;

	off = byteoff / 2;
	nw = nbytes / 2;

#ifdef BCMPCIEDEV
	if ((BUSTYPE(bustype) == SI_BUS) &&
		(BCM43602_CHIP(sih->chip) ||
		(BCM4369_CHIP(sih->chip)) ||
		(BCM4378_CHIP(sih->chip)) ||
		(BCM4387_CHIP(sih->chip)) ||
		(BCM4388_CHIP(sih->chip)) ||
		(BCM4362_CHIP(sih->chip)) ||
		(BCM4385_CHIP(sih->chip)) ||
		(BCM4389_CHIP(sih->chip)) ||
		(BCM4397_CHIP(sih->chip)) ||

#ifdef UNRELEASEDCHIP
#endif

	     FALSE)) { /* building firmware for chips with a PCIe interface and internal SI bus */
#else
	if (BUSTYPE(bustype) == PCI_BUS) {
#endif /* BCMPCIEDEV */
		if (!curmap)
			return 1;

		if (si_is_sprom_available(sih)) {
			volatile uint16 *srom;

			srom = (volatile uint16 *)srom_offset(sih, curmap);
			if (srom == NULL)
				return 1;

			if (sprom_read_pci(osh, sih, srom, off, buf, nw, check_crc))
				return 1;
		}
#if !defined(BCMDONGLEHOST) && (defined(BCMNVRAMW) || defined(BCMNVRAMR))
		else if (!((BUSTYPE(bustype) == SI_BUS) &&
			(BCM43602_CHIP(sih->chip) ||
			(BCM4369_CHIP(sih->chip)) ||
			(BCM4362_CHIP(sih->chip)) ||
			(BCM4378_CHIP(sih->chip)) ||
			(BCM4385_CHIP(sih->chip)) ||
			(BCM4389_CHIP(sih->chip)) ||
			(BCM4387_CHIP(sih->chip)) ||
			(BCM4388_CHIP(sih->chip)) ||
			(BCM4397_CHIP(sih->chip)) ||
			0))) {
			if (otp_read_pci(osh, sih, buf, nbytes))
				return 1;
		}
#endif /* !BCMDONGLEHOST && (BCMNVRAMW||BCMNVRAMR) */
#ifdef BCMSDIO
	} else if (BUSTYPE(bustype) == SDIO_BUS) {
		off = byteoff / 2;
		nw = nbytes / 2;
		for (i = 0; i < nw; i++) {
			if (sprom_read_sdio(osh, (uint16)(off + i), (uint16 *)(buf + i)))
				return 1;
		}
#endif /* BCMSDIO */
#ifdef BCMSPI
	} else if (BUSTYPE(bustype) == SPI_BUS) {
		if (bcmsdh_cis_read(NULL, SDIO_FUNC_1, (uint8 *)buf, byteoff + nbytes) != 0)
			return 1;
#endif /* BCMSPI */
	} else if (BUSTYPE(bustype) == SI_BUS) {
		return 1;
	} else {
		return 1;
	}

	return 0;
}

#if defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG)
/** support only 16-bit word write into srom */
int
srom_write(si_t *sih, uint bustype, volatile void *curmap, osl_t *osh,
           uint byteoff, uint nbytes, uint16 *buf)
{
	uint i, nw, crc_range;
	uint16 *old, *new;
	uint8 crc;
	volatile uint32 val32;
	int rc = 1;

	ASSERT(bustype == BUSTYPE(bustype));

	/* freed in same function */
	old = MALLOC_NOPERSIST(osh, SROM_MAXW * sizeof(uint16));
	new = MALLOC_NOPERSIST(osh, SROM_MAXW * sizeof(uint16));

	if (old == NULL || new == NULL)
		goto done;

	/* check input - 16-bit access only. use byteoff 0x55aa to indicate
	 * srclear
	 */
	if ((byteoff != 0x55aa) && ((byteoff & 1) || (nbytes & 1)))
		goto done;

	if ((byteoff != 0x55aa) && ((byteoff + nbytes) > SROM_MAX))
		goto done;

	if (FALSE) {
	}
#ifdef BCMSDIO
	else if (BUSTYPE(bustype) == SDIO_BUS) {
		crc_range = SROM_MAX;
	}
#endif
	else {
		crc_range = srom_size(sih, osh);
	}

	nw = crc_range / 2;
	/* read first small number words from srom, then adjust the length, read all */
	if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE))
		goto done;

	BS_ERROR(("srom_write: old[SROM4_SIGN] 0x%x, old[SROM8_SIGN] 0x%x\n",
	          old[SROM4_SIGN], old[SROM8_SIGN]));
	/* Deal with blank srom */
	if (old[0] == 0xffff) {
		/* Do nothing to blank srom when it's srclear */
		if (byteoff == 0x55aa) {
			rc = 0;
			goto done;
		}

		/* see if the input buffer is valid SROM image or not */
		if (buf[SROM11_SIGN] == SROM11_SIGNATURE) {
			BS_ERROR(("srom_write: buf[SROM11_SIGN] 0x%x\n",
				buf[SROM11_SIGN]));

			/* block invalid buffer size */
			if (nbytes < SROM11_WORDS * 2) {
				rc = BCME_BUFTOOSHORT;
				goto done;
			} else if (nbytes > SROM11_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}

			nw = SROM11_WORDS;

		} else if (buf[SROM12_SIGN] == SROM12_SIGNATURE) {
			BS_ERROR(("srom_write: buf[SROM12_SIGN] 0x%x\n",
				buf[SROM12_SIGN]));

			/* block invalid buffer size */
			if (nbytes < SROM12_WORDS * 2) {
				rc = BCME_BUFTOOSHORT;
				goto done;
			} else if (nbytes > SROM12_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}

			nw = SROM12_WORDS;

		} else if (buf[SROM13_SIGN] == SROM13_SIGNATURE) {
			BS_ERROR(("srom_write: buf[SROM13_SIGN] 0x%x\n",
				buf[SROM13_SIGN]));

			/* block invalid buffer size */
			if (nbytes < SROM13_WORDS * 2) {
				rc = BCME_BUFTOOSHORT;
				goto done;
			} else if (nbytes > SROM13_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}

			nw = SROM13_WORDS;

		} else if (buf[SROM16_SIGN] == SROM16_SIGNATURE) {
			BS_ERROR(("srom_write: buf[SROM16_SIGN] 0x%x\n",
				buf[SROM16_SIGN]));

			/* block invalid buffer size */
			if (nbytes < SROM16_WORDS * 2) {
				rc = BCME_BUFTOOSHORT;
				goto done;
			} else if (nbytes > SROM16_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}

			nw = SROM16_WORDS;

		} else if (buf[SROM17_SIGN] == SROM17_SIGNATURE) {
			BS_ERROR(("srom_write: buf[SROM17_SIGN] 0x%x\n",
				buf[SROM17_SIGN]));

			/* block invalid buffer size */
			if (nbytes < SROM17_WORDS * 2) {
				rc = BCME_BUFTOOSHORT;
				goto done;
			} else if (nbytes > SROM17_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}

			nw = SROM17_WORDS;
		} else if (buf[SROM18_SIGN] == SROM18_SIGNATURE) {
			BS_ERROR(("srom_write: buf[SROM18_SIGN] 0x%x\n",
				buf[SROM18_SIGN]));

			/* block invalid buffer size */
			/* nbytes can be < SROM18 bytes since host limits transfer chunk size
			 * to 1500 Bytes
			 */
			if (nbytes > SROM18_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}

			nw = SROM18_WORDS;

		} else if (buf[SROM11_SIGN] == SROM15_SIGNATURE) {
			BS_ERROR(("srom_write: buf[SROM15_SIGN] 0x%x\n",
				buf[SROM11_SIGN]));
			 /* nbytes can be < SROM15 bytes since host limits trasnfer chunk size
			 * to 1518 Bytes
			 */
			 if (nbytes > SROM15_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}
			nw = SROM15_WORDS;
		} else if ((buf[SROM4_SIGN] == SROM4_SIGNATURE) ||
			(buf[SROM8_SIGN] == SROM4_SIGNATURE)) {
			BS_ERROR(("srom_write: buf[SROM4_SIGN] 0x%x, buf[SROM8_SIGN] 0x%x\n",
				buf[SROM4_SIGN], buf[SROM8_SIGN]));

			/* block invalid buffer size */
			if (nbytes < SROM4_WORDS * 2) {
				rc = BCME_BUFTOOSHORT;
				goto done;
			} else if (nbytes > SROM4_WORDS * 2) {
				rc = BCME_BUFTOOLONG;
				goto done;
			}

			nw = SROM4_WORDS;
		} else if (nbytes == SROM_WORDS * 2){ /* the other possible SROM format */
			BS_ERROR(("srom_write: Not SROM4 or SROM8.\n"));

			nw = SROM_WORDS;
		} else {
			BS_ERROR(("srom_write: Invalid input file signature\n"));
			rc = BCME_BADARG;
			goto done;
		}
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if (old[SROM18_SIGN] == SROM18_SIGNATURE) {
		nw = SROM18_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if (old[SROM17_SIGN] == SROM17_SIGNATURE) {
		nw = SROM17_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if (old[SROM16_SIGN] == SROM16_SIGNATURE) {
		nw = SROM16_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if (old[SROM15_SIGN] == SROM15_SIGNATURE) {
		nw = SROM15_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if (old[SROM13_SIGN] == SROM13_SIGNATURE) {
		nw = SROM13_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if (old[SROM12_SIGN] == SROM12_SIGNATURE) {
		nw = SROM12_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if (old[SROM11_SIGN] == SROM11_SIGNATURE) {
		nw = SROM11_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else if ((old[SROM4_SIGN] == SROM4_SIGNATURE) ||
	           (old[SROM8_SIGN] == SROM4_SIGNATURE)) {
		nw = SROM4_WORDS;
		crc_range = nw * 2;
		if (srom_read(sih, bustype, curmap, osh, 0, crc_range, old, FALSE)) {
			goto done;
		}
	} else {
		/* Assert that we have already read enough for sromrev 2 */
		ASSERT(crc_range >= SROM_WORDS * 2);
		nw = SROM_WORDS;
		crc_range = nw * 2;
	}

	if (byteoff == 0x55aa) {
		/* Erase request */
		crc_range = 0;
		memset((void *)new, 0xff, nw * 2);
	} else {
		/* Copy old contents */
		bcopy((void *)old, (void *)new, nw * 2);
		/* make changes */
		bcopy((void *)buf, (void *)&new[byteoff / 2], nbytes);
	}

	if (crc_range) {
		/* calculate crc */
		htol16_buf(new, crc_range);
		crc = ~hndcrc8((uint8 *)new, crc_range - 1, CRC8_INIT_VALUE);
		ltoh16_buf(new, crc_range);
		new[nw - 1] = (crc << 8) | (new[nw - 1] & 0xff);
	}

#ifdef BCMPCIEDEV
	if ((BUSTYPE(bustype) == SI_BUS) &&
		(BCM43602_CHIP(sih->chip) ||
		(BCM4369_CHIP(sih->chip)) ||
		(BCM4362_CHIP(sih->chip)) ||
		(BCM4378_CHIP(sih->chip)) ||
		(BCM4387_CHIP(sih->chip)) ||
		(BCM4388_CHIP(sih->chip)) ||
		(BCM4385_CHIP(sih->chip)) ||
		(BCM4389_CHIP(sih->chip)) ||
		(BCM4397_CHIP(sih->chip)) ||

#ifdef UNRELEASEDCHIP
#endif /* UNRELEASEDCHIP */

	     FALSE)) {
#else
	if (BUSTYPE(bustype) == PCI_BUS) {
#endif /* BCMPCIEDEV */
		volatile uint16 *srom = NULL;
		volatile void *ccregs = NULL;
		uint32 ccval = 0;

		if ((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM43526_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM4352_CHIP_ID) ||
		    BCM43602_CHIP(sih->chip)) {
			/* save current control setting */
			ccval = si_chipcontrl_read(sih);
		}

		if (BCM43602_CHIP(sih->chip) ||
			(((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
			(CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
			(CHIPID(sih->chip) == BCM4352_CHIP_ID)) &&
			(CHIPREV(sih->chiprev) <= 2))) {
			si_chipcontrl_srom4360(sih, TRUE);
		}

		if (FALSE) {
			si_srom_clk_set(sih); /* corrects srom clock frequency */
		}

		/* enable writes to the SPROM */
		if (sih->ccrev > 31) {
			if (BUSTYPE(sih->bustype) == SI_BUS)
				ccregs = (void *)(uintptr)SI_ENUM_BASE(sih);
			else
				ccregs = ((volatile uint8 *)curmap + PCI_16KB0_CCREGS_OFFSET);
			srom = (volatile uint16 *)((volatile uint8 *)ccregs + CC_SROM_OTP);
			(void)srom_cc_cmd(sih, osh, ccregs, SRC_OP_WREN, 0, 0);
		} else {
			srom = (volatile uint16 *)
					((volatile uint8 *)curmap + PCI_BAR0_SPROM_OFFSET);
			val32 = OSL_PCI_READ_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32));
			val32 |= SPROM_WRITEEN;
			OSL_PCI_WRITE_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32), val32);
		}
		bcm_mdelay(WRITE_ENABLE_DELAY);
		/* write srom */
		for (i = 0; i < nw; i++) {
			if (old[i] != new[i]) {
				if (sih->ccrev > 31) {
					if ((sih->cccaps & CC_CAP_SROM) == 0) {
						/* No srom support in this chip */
						BS_ERROR(("srom_write, invalid srom, skip\n"));
					} else
						(void)srom_cc_cmd(sih, osh, ccregs, SRC_OP_WRITE,
							i, new[i]);
				} else {
					W_REG(osh, &srom[i], new[i]);
				}
				bcm_mdelay(WRITE_WORD_DELAY);
			}
		}
		/* disable writes to the SPROM */
		if (sih->ccrev > 31) {
			(void)srom_cc_cmd(sih, osh, ccregs, SRC_OP_WRDIS, 0, 0);
		} else {
			OSL_PCI_WRITE_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32), val32 &
			                     ~SPROM_WRITEEN);
		}

		if (BCM43602_CHIP(sih->chip) ||
		    (CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM4352_CHIP_ID)) {
			/* Restore config after reading SROM */
			si_chipcontrl_restore(sih, ccval);
		}
#ifdef BCMSDIO
	} else if (BUSTYPE(bustype) == SDIO_BUS) {
		/* enable writes to the SPROM */
		if (sprom_cmd_sdio(osh, SBSDIO_SPROM_WEN))
			goto done;
		bcm_mdelay(WRITE_ENABLE_DELAY);
		/* write srom */
		for (i = 0; i < nw; i++) {
			if (old[i] != new[i]) {
				sprom_write_sdio(osh, (uint16)(i), new[i]);
				bcm_mdelay(WRITE_WORD_DELAY);
			}
		}
		/* disable writes to the SPROM */
		if (sprom_cmd_sdio(osh, SBSDIO_SPROM_WDS))
			goto done;
#endif /* BCMSDIO */
	} else if (BUSTYPE(bustype) == SI_BUS) {
		goto done;
	} else {
		goto done;
	}

	bcm_mdelay(WRITE_ENABLE_DELAY);
	rc = 0;

done:
	if (old != NULL)
		MFREE(osh, old, SROM_MAXW * sizeof(uint16));
	if (new != NULL)
		MFREE(osh, new, SROM_MAXW * sizeof(uint16));

	return rc;
}

/** support only 16-bit word write into srom */
int
srom_write_short(si_t *sih, uint bustype, volatile void *curmap, osl_t *osh,
                 uint byteoff, uint16 value)
{
	volatile uint32 val32;
	int rc = 1;

	ASSERT(bustype == BUSTYPE(bustype));

	if (byteoff & 1)
		goto done;

#ifdef BCMPCIEDEV
	if ((BUSTYPE(bustype) == SI_BUS) &&
	    (BCM43602_CHIP(sih->chip) ||
	     FALSE)) {
#else
	if (BUSTYPE(bustype) == PCI_BUS) {
#endif /* BCMPCIEDEV */
		volatile uint16 *srom = NULL;
		volatile void *ccregs = NULL;
		uint32 ccval = 0;

		if (BCM43602_CHIP(sih->chip) ||
		    (CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM43526_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM4352_CHIP_ID)) {
			/* save current control setting */
			ccval = si_chipcontrl_read(sih);
		}

		if (BCM43602_CHIP(sih->chip) ||
			(((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
			(CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
			(CHIPID(sih->chip) == BCM4352_CHIP_ID)) &&
			(CHIPREV(sih->chiprev) <= 2))) {
			si_chipcontrl_srom4360(sih, TRUE);
		}

		if (FALSE) {
			si_srom_clk_set(sih); /* corrects srom clock frequency */
		}

		/* enable writes to the SPROM */
		if (sih->ccrev > 31) {
			if (BUSTYPE(sih->bustype) == SI_BUS)
				ccregs = (void *)(uintptr)SI_ENUM_BASE(sih);
			else
				ccregs = ((volatile uint8 *)curmap + PCI_16KB0_CCREGS_OFFSET);
			srom = (volatile uint16 *)((volatile uint8 *)ccregs + CC_SROM_OTP);
			(void)srom_cc_cmd(sih, osh, ccregs, SRC_OP_WREN, 0, 0);
		} else {
			srom = (volatile uint16 *)
					((volatile uint8 *)curmap + PCI_BAR0_SPROM_OFFSET);
			val32 = OSL_PCI_READ_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32));
			val32 |= SPROM_WRITEEN;
			OSL_PCI_WRITE_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32), val32);
		}
		bcm_mdelay(WRITE_ENABLE_DELAY);
		/* write srom */
		if (sih->ccrev > 31) {
			if ((sih->cccaps & CC_CAP_SROM) == 0) {
				/* No srom support in this chip */
				BS_ERROR(("srom_write, invalid srom, skip\n"));
			} else
				(void)srom_cc_cmd(sih, osh, ccregs, SRC_OP_WRITE,
				                   byteoff/2, value);
		} else {
			W_REG(osh, &srom[byteoff/2], value);
		}
		bcm_mdelay(WRITE_WORD_DELAY);

		/* disable writes to the SPROM */
		if (sih->ccrev > 31) {
			(void)srom_cc_cmd(sih, osh, ccregs, SRC_OP_WRDIS, 0, 0);
		} else {
			OSL_PCI_WRITE_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32), val32 &
			                     ~SPROM_WRITEEN);
		}

		if (BCM43602_CHIP(sih->chip) ||
		    (CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM43526_CHIP_ID) ||
		    (CHIPID(sih->chip) == BCM4352_CHIP_ID)) {
			/* Restore config after reading SROM */
			si_chipcontrl_restore(sih, ccval);
		}
#ifdef BCMSDIO
	} else if (BUSTYPE(bustype) == SDIO_BUS) {
		/* enable writes to the SPROM */
		if (sprom_cmd_sdio(osh, SBSDIO_SPROM_WEN))
			goto done;
		bcm_mdelay(WRITE_ENABLE_DELAY);
		/* write srom */
		sprom_write_sdio(osh, (uint16)(byteoff/2), value);
		bcm_mdelay(WRITE_WORD_DELAY);

		/* disable writes to the SPROM */
		if (sprom_cmd_sdio(osh, SBSDIO_SPROM_WDS))
			goto done;
#endif /* BCMSDIO */
	} else if (BUSTYPE(bustype) == SI_BUS) {
		goto done;
	} else {
		goto done;
	}

	bcm_mdelay(WRITE_ENABLE_DELAY);
	rc = 0;

done:
	return rc;
}
#endif /* defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG) */

/**
 * These 'vstr_*' definitions are used to convert from CIS format to a 'NVRAM var=val' format, the
 * NVRAM format is used throughout the rest of the firmware.
 */
#if !defined(BCMDONGLEHOST)
static const char BCMATTACHDATA(vstr_manf)[] = "manf=%s";
static const char BCMATTACHDATA(vstr_productname)[] = "productname=%s";
static const char BCMATTACHDATA(vstr_manfid)[] = "manfid=0x%x";
static const char BCMATTACHDATA(vstr_prodid)[] = "prodid=0x%x";
#ifdef BCMSDIO
static const char BCMATTACHDATA(vstr_sdmaxspeed)[] = "sdmaxspeed=%d";
static const char BCMATTACHDATA(vstr_sdmaxblk)[][13] =
	{ "sdmaxblk0=%d", "sdmaxblk1=%d", "sdmaxblk2=%d" };
#endif
static const char BCMATTACHDATA(vstr_regwindowsz)[] = "regwindowsz=%d";
static const char BCMATTACHDATA(vstr_sromrev)[] = "sromrev=%d";
static const char BCMATTACHDATA(vstr_chiprev)[] = "chiprev=%d";
static const char BCMATTACHDATA(vstr_subvendid)[] = "subvendid=0x%x";
static const char BCMATTACHDATA(vstr_subdevid)[] = "subdevid=0x%x";
static const char BCMATTACHDATA(vstr_boardrev)[] = "boardrev=0x%x";
static const char BCMATTACHDATA(vstr_aa2g)[] = "aa2g=0x%x";
static const char BCMATTACHDATA(vstr_aa5g)[] = "aa5g=0x%x";
static const char BCMATTACHDATA(vstr_ag)[] = "ag%d=0x%x";
static const char BCMATTACHDATA(vstr_cc)[] = "cc=%d";
static const char BCMATTACHDATA(vstr_opo)[] = "opo=%d";
static const char BCMATTACHDATA(vstr_pa0b)[][9] = { "pa0b0=%d", "pa0b1=%d", "pa0b2=%d" };
static const char BCMATTACHDATA(vstr_pa0b_lo)[][12] =
	{ "pa0b0_lo=%d", "pa0b1_lo=%d", "pa0b2_lo=%d" };
static const char BCMATTACHDATA(vstr_pa0itssit)[] = "pa0itssit=%d";
static const char BCMATTACHDATA(vstr_pa0maxpwr)[] = "pa0maxpwr=%d";
static const char BCMATTACHDATA(vstr_pa1b)[][9] = { "pa1b0=%d", "pa1b1=%d", "pa1b2=%d" };
static const char BCMATTACHDATA(vstr_pa1lob)[][11] =
	{ "pa1lob0=%d", "pa1lob1=%d", "pa1lob2=%d" };
static const char BCMATTACHDATA(vstr_pa1hib)[][11] =
	{ "pa1hib0=%d", "pa1hib1=%d", "pa1hib2=%d" };
static const char BCMATTACHDATA(vstr_pa1itssit)[] = "pa1itssit=%d";
static const char BCMATTACHDATA(vstr_pa1maxpwr)[] = "pa1maxpwr=%d";
static const char BCMATTACHDATA(vstr_pa1lomaxpwr)[] = "pa1lomaxpwr=%d";
static const char BCMATTACHDATA(vstr_pa1himaxpwr)[] = "pa1himaxpwr=%d";
static const char BCMATTACHDATA(vstr_oem)[] = "oem=%02x%02x%02x%02x%02x%02x%02x%02x";
static const char BCMATTACHDATA(vstr_boardflags)[] = "boardflags=0x%x";
static const char BCMATTACHDATA(vstr_boardflags2)[] = "boardflags2=0x%x";
static const char BCMATTACHDATA(vstr_boardflags3)[] = "boardflags3=0x%x";
static const char BCMATTACHDATA(vstr_boardflags4)[] = "boardflags4=0x%x";
static const char BCMATTACHDATA(vstr_boardflags5)[] = "boardflags5=0x%x";
static const char BCMATTACHDATA(vstr_noccode)[] = "ccode=0x0";
static const char BCMATTACHDATA(vstr_ccode)[] = "ccode=%c%c";
static const char BCMATTACHDATA(vstr_cctl)[] = "cctl=0x%x";
static const char BCMATTACHDATA(vstr_cckpo)[] = "cckpo=0x%x";
static const char BCMATTACHDATA(vstr_ofdmpo)[] = "ofdmpo=0x%x";
static const char BCMATTACHDATA(vstr_rdlid)[] = "rdlid=0x%x";
#ifdef BCM_BOOTLOADER
static const char BCMATTACHDATA(vstr_rdlrndis)[] = "rdlrndis=%d";
static const char BCMATTACHDATA(vstr_rdlrwu)[] = "rdlrwu=%d";
static const char BCMATTACHDATA(vstr_rdlsn)[] = "rdlsn=%d";
#endif /* BCM_BOOTLOADER */
static const char BCMATTACHDATA(vstr_usbfs)[] = "usbfs=%d";
static const char BCMATTACHDATA(vstr_wpsgpio)[] = "wpsgpio=%d";
static const char BCMATTACHDATA(vstr_wpsled)[] = "wpsled=%d";
static const char BCMATTACHDATA(vstr_rssismf2g)[] = "rssismf2g=%d";
static const char BCMATTACHDATA(vstr_rssismc2g)[] = "rssismc2g=%d";
static const char BCMATTACHDATA(vstr_rssisav2g)[] = "rssisav2g=%d";
static const char BCMATTACHDATA(vstr_bxa2g)[] = "bxa2g=%d";
static const char BCMATTACHDATA(vstr_rssismf5g)[] = "rssismf5g=%d";
static const char BCMATTACHDATA(vstr_rssismc5g)[] = "rssismc5g=%d";
static const char BCMATTACHDATA(vstr_rssisav5g)[] = "rssisav5g=%d";
static const char BCMATTACHDATA(vstr_bxa5g)[] = "bxa5g=%d";
static const char BCMATTACHDATA(vstr_tri2g)[] = "tri2g=%d";
static const char BCMATTACHDATA(vstr_tri5gl)[] = "tri5gl=%d";
static const char BCMATTACHDATA(vstr_tri5g)[] = "tri5g=%d";
static const char BCMATTACHDATA(vstr_tri5gh)[] = "tri5gh=%d";
static const char BCMATTACHDATA(vstr_rxpo2g)[] = "rxpo2g=%d";
static const char BCMATTACHDATA(vstr_rxpo5g)[] = "rxpo5g=%d";
static const char BCMATTACHDATA(vstr_boardtype)[] = "boardtype=0x%x";
static const char BCMATTACHDATA(vstr_vendid)[] = "vendid=0x%x";
static const char BCMATTACHDATA(vstr_devid)[] = "devid=0x%x";
static const char BCMATTACHDATA(vstr_xtalfreq)[] = "xtalfreq=%d";
static const char BCMATTACHDATA(vstr_txchain)[] = "txchain=0x%x";
static const char BCMATTACHDATA(vstr_rxchain)[] = "rxchain=0x%x";
static const char BCMATTACHDATA(vstr_elna2g)[] = "elna2g=0x%x";
static const char BCMATTACHDATA(vstr_elna5g)[] = "elna5g=0x%x";
static const char BCMATTACHDATA(vstr_antswitch)[] = "antswitch=0x%x";
static const char BCMATTACHDATA(vstr_regrev)[] = "regrev=0x%x";
static const char BCMATTACHDATA(vstr_antswctl2g)[] = "antswctl2g=0x%x";
static const char BCMATTACHDATA(vstr_triso2g)[] = "triso2g=0x%x";
static const char BCMATTACHDATA(vstr_pdetrange2g)[] = "pdetrange2g=0x%x";
static const char BCMATTACHDATA(vstr_extpagain2g)[] = "extpagain2g=0x%x";
static const char BCMATTACHDATA(vstr_tssipos2g)[] = "tssipos2g=0x%x";
static const char BCMATTACHDATA(vstr_antswctl5g)[] = "antswctl5g=0x%x";
static const char BCMATTACHDATA(vstr_triso5g)[] = "triso5g=0x%x";
static const char BCMATTACHDATA(vstr_pdetrange5g)[] = "pdetrange5g=0x%x";
static const char BCMATTACHDATA(vstr_extpagain5g)[] = "extpagain5g=0x%x";
static const char BCMATTACHDATA(vstr_tssipos5g)[] = "tssipos5g=0x%x";
static const char BCMATTACHDATA(vstr_maxp2ga)[] = "maxp2ga%d=0x%x";
static const char BCMATTACHDATA(vstr_itt2ga0)[] = "itt2ga0=0x%x";
static const char BCMATTACHDATA(vstr_pa)[] = "pa%dgw%da%d=0x%x";
static const char BCMATTACHDATA(vstr_pahl)[] = "pa%dg%cw%da%d=0x%x";
static const char BCMATTACHDATA(vstr_maxp5ga0)[] = "maxp5ga0=0x%x";
static const char BCMATTACHDATA(vstr_itt5ga0)[] = "itt5ga0=0x%x";
static const char BCMATTACHDATA(vstr_maxp5gha0)[] = "maxp5gha0=0x%x";
static const char BCMATTACHDATA(vstr_maxp5gla0)[] = "maxp5gla0=0x%x";
static const char BCMATTACHDATA(vstr_itt2ga1)[] = "itt2ga1=0x%x";
static const char BCMATTACHDATA(vstr_maxp5ga1)[] = "maxp5ga1=0x%x";
static const char BCMATTACHDATA(vstr_itt5ga1)[] = "itt5ga1=0x%x";
static const char BCMATTACHDATA(vstr_maxp5gha1)[] = "maxp5gha1=0x%x";
static const char BCMATTACHDATA(vstr_maxp5gla1)[] = "maxp5gla1=0x%x";
static const char BCMATTACHDATA(vstr_cck2gpo)[] = "cck2gpo=0x%x";
static const char BCMATTACHDATA(vstr_ofdm2gpo)[] = "ofdm2gpo=0x%x";
static const char BCMATTACHDATA(vstr_ofdm5gpo)[] = "ofdm5gpo=0x%x";
static const char BCMATTACHDATA(vstr_ofdm5glpo)[] = "ofdm5glpo=0x%x";
static const char BCMATTACHDATA(vstr_ofdm5ghpo)[] = "ofdm5ghpo=0x%x";
static const char BCMATTACHDATA(vstr_cddpo)[] = "cddpo=0x%x";
static const char BCMATTACHDATA(vstr_stbcpo)[] = "stbcpo=0x%x";
static const char BCMATTACHDATA(vstr_bw40po)[] = "bw40po=0x%x";
static const char BCMATTACHDATA(vstr_bwduppo)[] = "bwduppo=0x%x";
static const char BCMATTACHDATA(vstr_mcspo)[] = "mcs%dgpo%d=0x%x";
static const char BCMATTACHDATA(vstr_mcspohl)[] = "mcs%dg%cpo%d=0x%x";
static const char BCMATTACHDATA(vstr_custom)[] = "customvar%d=0x%x";
static const char BCMATTACHDATA(vstr_cckdigfilttype)[] = "cckdigfilttype=%d";
static const char BCMATTACHDATA(vstr_usbflags)[] = "usbflags=0x%x";
#ifdef BCM_BOOTLOADER
static const char BCMATTACHDATA(vstr_mdio)[] = "mdio%d=0x%%x";
static const char BCMATTACHDATA(vstr_mdioex)[] = "mdioex%d=0x%%x";
static const char BCMATTACHDATA(vstr_brmin)[] = "brmin=0x%x";
static const char BCMATTACHDATA(vstr_brmax)[] = "brmax=0x%x";
static const char BCMATTACHDATA(vstr_pllreg)[] = "pll%d=0x%x";
static const char BCMATTACHDATA(vstr_ccreg)[] = "chipc%d=0x%x";
static const char BCMATTACHDATA(vstr_regctrl)[] = "reg%d=0x%x";
static const char BCMATTACHDATA(vstr_time)[] = "r%dt=0x%x";
static const char BCMATTACHDATA(vstr_depreg)[] = "r%dd=0x%x";
static const char BCMATTACHDATA(vstr_usbpredly)[] = "usbpredly=0x%x";
static const char BCMATTACHDATA(vstr_usbpostdly)[] = "usbpostdly=0x%x";
static const char BCMATTACHDATA(vstr_usbrdy)[] = "usbrdy=0x%x";
static const char BCMATTACHDATA(vstr_hsicphyctrl1)[] = "hsicphyctrl1=0x%x";
static const char BCMATTACHDATA(vstr_hsicphyctrl2)[] = "hsicphyctrl2=0x%x";
static const char BCMATTACHDATA(vstr_usbdevctrl)[] = "usbdevctrl=0x%x";
static const char BCMATTACHDATA(vstr_bldr_reset_timeout)[] = "bldr_to=0x%x";
static const char BCMATTACHDATA(vstr_muxenab)[] = "muxenab=0x%x";
static const char BCMATTACHDATA(vstr_pubkey)[] = "pubkey=%s";
#endif /* BCM_BOOTLOADER */
static const char BCMATTACHDATA(vstr_boardnum)[] = "boardnum=%d";
static const char BCMATTACHDATA(vstr_macaddr)[] = "macaddr=%s";
static const char BCMATTACHDATA(vstr_macaddr2)[] = "macaddr2=%s";
static const char BCMATTACHDATA(vstr_usbepnum)[] = "usbepnum=0x%x";
#ifdef BCMUSBDEV_COMPOSITE
static const char BCMATTACHDATA(vstr_usbdesc_composite)[] = "usbdesc_composite=0x%x";
#endif /* BCMUSBDEV_COMPOSITE */
static const char BCMATTACHDATA(vstr_usbutmi_ctl)[] = "usbutmi_ctl=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_utmi_ctl0)[] = "usbssphy_utmi_ctl0=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_utmi_ctl1)[] = "usbssphy_utmi_ctl1=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_utmi_ctl2)[] = "usbssphy_utmi_ctl2=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_sleep0)[] = "usbssphy_sleep0=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_sleep1)[] = "usbssphy_sleep1=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_sleep2)[] = "usbssphy_sleep2=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_sleep3)[] = "usbssphy_sleep3=0x%x";
static const char BCMATTACHDATA(vstr_usbssphy_mdio)[] = "usbssmdio%d=0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_usb30phy_noss)[] = "usbnoss=0x%x";
static const char BCMATTACHDATA(vstr_usb30phy_u1u2)[] = "usb30u1u2=0x%x";
static const char BCMATTACHDATA(vstr_usb30phy_regs)[] = "usb30regs%d=0x%x,0x%x,0x%x,0x%x";

/* Power per rate for SROM V9 */
static const char BCMATTACHDATA(vstr_cckbw202gpo)[][21] =
	{ "cckbw202gpo=0x%x", "cckbw20ul2gpo=0x%x", "cckbw20in802gpo=0x%x" };
static const char BCMATTACHDATA(vstr_legofdmbw202gpo)[][23] =
	{ "legofdmbw202gpo=0x%x", "legofdmbw20ul2gpo=0x%x" };
static const char BCMATTACHDATA(vstr_legofdmbw205gpo)[][24] =
	{ "legofdmbw205glpo=0x%x", "legofdmbw20ul5glpo=0x%x",
	"legofdmbw205gmpo=0x%x", "legofdmbw20ul5gmpo=0x%x",
	"legofdmbw205ghpo=0x%x", "legofdmbw20ul5ghpo=0x%x" };

static const char BCMATTACHDATA(vstr_mcs2gpo)[][19] =
{ "mcsbw202gpo=0x%x", "mcsbw20ul2gpo=0x%x", "mcsbw402gpo=0x%x", "mcsbw802gpo=0x%x" };

static const char BCMATTACHDATA(vstr_mcs5glpo)[][20] =
	{ "mcsbw205glpo=0x%x", "mcsbw20ul5glpo=0x%x", "mcsbw405glpo=0x%x" };

static const char BCMATTACHDATA(vstr_mcs5gmpo)[][20] =
	{ "mcsbw205gmpo=0x%x", "mcsbw20ul5gmpo=0x%x", "mcsbw405gmpo=0x%x" };

static const char BCMATTACHDATA(vstr_mcs5ghpo)[][20] =
	{ "mcsbw205ghpo=0x%x", "mcsbw20ul5ghpo=0x%x", "mcsbw405ghpo=0x%x" };

static const char BCMATTACHDATA(vstr_mcs32po)[] = "mcs32po=0x%x";
static const char BCMATTACHDATA(vstr_legofdm40duppo)[] = "legofdm40duppo=0x%x";

/* SROM V11 */
static const char BCMATTACHDATA(vstr_tempthresh)[] = "tempthresh=%d";	/* HNBU_TEMPTHRESH */
static const char BCMATTACHDATA(vstr_temps_period)[] = "temps_period=%d";
static const char BCMATTACHDATA(vstr_temps_hysteresis)[] = "temps_hysteresis=%d";
static const char BCMATTACHDATA(vstr_tempoffset)[] = "tempoffset=%d";
static const char BCMATTACHDATA(vstr_tempsense_slope)[] = "tempsense_slope=%d";
static const char BCMATTACHDATA(vstr_temp_corrx)[] = "tempcorrx=%d";
static const char BCMATTACHDATA(vstr_tempsense_option)[] = "tempsense_option=%d";
static const char BCMATTACHDATA(vstr_phycal_tempdelta)[] = "phycal_tempdelta=%d";
static const char BCMATTACHDATA(vstr_tssiposslopeg)[] = "tssiposslope%dg=%d";	/* HNBU_FEM_CFG */
static const char BCMATTACHDATA(vstr_epagaing)[] = "epagain%dg=%d";
static const char BCMATTACHDATA(vstr_pdgaing)[] = "pdgain%dg=%d";
static const char BCMATTACHDATA(vstr_tworangetssi)[] = "tworangetssi%dg=%d";
static const char BCMATTACHDATA(vstr_papdcap)[] = "papdcap%dg=%d";
static const char BCMATTACHDATA(vstr_femctrl)[] = "femctrl=%d";
static const char BCMATTACHDATA(vstr_gainctrlsph)[] = "gainctrlsph=%d";
static const char BCMATTACHDATA(vstr_subband5gver)[] = "subband5gver=%d";	/* HNBU_ACPA_CX */
static const char BCMATTACHDATA(vstr_pa2ga)[] = "pa2ga%d=0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_maxp5ga)[] = "maxp5ga%d=0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_pa5ga)[] = "pa5ga%d=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,"
	"0x%x,0x%x,0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_subband6gver)[] = "subband6gver=%d";	/* HNBU_ACPA_CX */
static const char BCMATTACHDATA(vstr_maxp6ga)[] = "maxp6ga%d=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_pa6ga)[] = "pa6ga%d=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,"
	"0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_pa2gccka)[] = "pa2gccka%d=0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_pa5gbw40a)[] = "pa5gbw40a%d=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,"
	"0x%x,0x%x,0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_pa5gbw80a)[] = "pa5gbw80a%d=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,"
	"0x%x,0x%x,0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_pa5gbw4080a)[] = "pa5gbw4080a%d=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,"
	"0x%x,0x%x,0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_rxgainsgelnagaina)[] = "rxgains%dgelnagaina%d=%d";
static const char BCMATTACHDATA(vstr_rxgainsgtrisoa)[] = "rxgains%dgtrisoa%d=%d";
static const char BCMATTACHDATA(vstr_rxgainsgtrelnabypa)[] = "rxgains%dgtrelnabypa%d=%d";
static const char BCMATTACHDATA(vstr_rxgainsgxelnagaina)[] = "rxgains%dg%celnagaina%d=%d";
static const char BCMATTACHDATA(vstr_rxgainsgxtrisoa)[] = "rxgains%dg%ctrisoa%d=%d";
static const char BCMATTACHDATA(vstr_rxgainsgxtrelnabypa)[] = "rxgains%dg%ctrelnabypa%d=%d";
static const char BCMATTACHDATA(vstr_measpower)[] = "measpower=0x%x";	/* HNBU_MEAS_PWR */
static const char BCMATTACHDATA(vstr_measpowerX)[] = "measpower%d=0x%x";
static const char BCMATTACHDATA(vstr_pdoffsetma)[] = "pdoffset%dma%d=0x%x";	/* HNBU_PDOFF */
static const char BCMATTACHDATA(vstr_pdoffset2gma)[] = "pdoffset2g%dma%d=0x%x";	/* HNBU_PDOFF_2G */
static const char BCMATTACHDATA(vstr_pdoffset2gmvalid)[] = "pdoffset2g%dmvalid=0x%x";
static const char BCMATTACHDATA(vstr_rawtempsense)[] = "rawtempsense=0x%x";
/* HNBU_ACPPR_2GPO */
static const char BCMATTACHDATA(vstr_dot11agofdmhrbw202gpo)[] = "dot11agofdmhrbw202gpo=0x%x";
static const char BCMATTACHDATA(vstr_ofdmlrbw202gpo)[] = "ofdmlrbw202gpo=0x%x";
static const char BCMATTACHDATA(vstr_mcsbw805gpo)[] = "mcsbw805g%cpo=0x%x"; /* HNBU_ACPPR_5GPO */
static const char BCMATTACHDATA(vstr_mcsbw1605gpo)[] = "mcsbw1605g%cpo=0x%x";
static const char BCMATTACHDATA(vstr_mcsbw80p805gpo)[] = "mcsbw80p805g%cpo=0x%x";
static const char BCMATTACHDATA(vstr_mcsbw80p805g1po)[] = "mcsbw80p805g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_mcsbw1605g1po)[] = "mcsbw1605g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_mcsbw805g1po)[] = "mcsbw805g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_mcsbw405g1po)[] = "mcsbw405g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_mcsbw205g1po)[] = "mcsbw205g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_mcslr5gpo)[] = "mcslr5g%cpo=0x%x";
static const char BCMATTACHDATA(vstr_mcslr5g1po)[] = "mcslr5g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_mcslr5g80p80po)[] = "mcslr5g80p80po=0x%x";
/* HNBU_ACPPR_SBPO */
static const char BCMATTACHDATA(vstr_sb20in40rpo)[] = "sb20in40%crpo=0x%x";
/* HNBU_ACPPR_SBPO */
static const char BCMATTACHDATA(vstr_sb20in40and80rpo)[] = "sb20in40and80%crpo=0x%x";
static const char BCMATTACHDATA(vstr_sb20in80and160r5gpo)[] = "sb20in80and160%cr5g%cpo=0x%x";
static const char BCMATTACHDATA(vstr_sb20in80and160r5g1po)[] = "sb20in80and160%cr5g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_sb2040and80in80p80r5gpo)[] =
	"sb2040and80in80p80%cr5g%cpo=0x%x";
static const char BCMATTACHDATA(vstr_sb2040and80in80p80r5g1po)[] =
	"sb2040and80in80p80%cr5g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_sb20in40dot11agofdm2gpo)[] = "sb20in40dot11agofdm2gpo=0x%x";
static const char BCMATTACHDATA(vstr_sb20in80dot11agofdm2gpo)[] = "sb20in80dot11agofdm2gpo=0x%x";
static const char BCMATTACHDATA(vstr_sb20in40ofdmlrbw202gpo)[] = "sb20in40ofdmlrbw202gpo=0x%x";
static const char BCMATTACHDATA(vstr_sb20in80ofdmlrbw202gpo)[] = "sb20in80ofdmlrbw202gpo=0x%x";
static const char BCMATTACHDATA(vstr_sb20in80p80r5gpo)[] = "sb20in80p80%cr5gpo=0x%x";
static const char BCMATTACHDATA(vstr_sb40and80r5gpo)[] = "sb40and80%cr5g%cpo=0x%x";
static const char BCMATTACHDATA(vstr_sb40and80r5g1po)[] = "sb40and80%cr5g%c1po=0x%x";
static const char BCMATTACHDATA(vstr_dot11agduprpo)[] = "dot11agdup%crpo=0x%x";
static const char BCMATTACHDATA(vstr_dot11agduppo)[] = "dot11agduppo=0x%x";
static const char BCMATTACHDATA(vstr_noiselvl2ga)[] = "noiselvl2ga%d=%d";	/* HNBU_NOISELVL */
static const char BCMATTACHDATA(vstr_noiselvl5ga)[] = "noiselvl5ga%d=%d,%d,%d,%d";
/* HNBU_RXGAIN_ERR */
static const char BCMATTACHDATA(vstr_rxgainerr2ga)[] = "rxgainerr2ga%d=0x%x";
static const char BCMATTACHDATA(vstr_rxgainerr5ga)[] = "rxgainerr5ga%d=0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_agbg)[] = "agbg%d=0x%x";	/* HNBU_AGBGA */
static const char BCMATTACHDATA(vstr_aga)[] = "aga%d=0x%x";
static const char BCMATTACHDATA(vstr_txduty_ofdm)[] = "tx_duty_cycle_ofdm_%d_5g=%d";
static const char BCMATTACHDATA(vstr_txduty_thresh)[] = "tx_duty_cycle_thresh_%d_5g=%d";
static const char BCMATTACHDATA(vstr_paparambwver)[] = "paparambwver=%d";

static const char BCMATTACHDATA(vstr_uuid)[] = "uuid=%s";

static const char BCMATTACHDATA(vstr_wowlgpio)[] = "wowl_gpio=%d";
static const char BCMATTACHDATA(vstr_wowlgpiopol)[] = "wowl_gpiopol=%d";

static const char BCMATTACHDATA(rstr_ag0)[] = "ag0";
static const char BCMATTACHDATA(rstr_sromrev)[] = "sromrev";

static const char BCMATTACHDATA(vstr_paparamrpcalvars)[][20] =
	{"rpcal2g=0x%x", "rpcal5gb0=0x%x", "rpcal5gb1=0x%x",
	"rpcal5gb2=0x%x", "rpcal5gb3=0x%x"};

static const char BCMATTACHDATA(vstr_gpdn)[] = "gpdn=0x%x";

/* SROM V13 PA */
static const char BCMATTACHDATA(vstr_sr13pa2ga)[] = "pa2ga%d=0x%x,0x%x,0x%x,0x%x";
static const char BCMATTACHDATA(vstr_maxp5gba)[] = "maxp5gb%da%d=0x%x";
static const char BCMATTACHDATA(vstr_sr13pa5ga)[] = "pa5ga%d=%s";
static const char BCMATTACHDATA(vstr_sr13pa5gbwa)[] = "pa5g%da%d=%s";
static const char BCMATTACHDATA(vstr_pa2g40a)[] = "pa2g40a%d=0x%x,0x%x,0x%x,0x%x";

/* RSSI Cal parameters */
static const char BCMATTACHDATA(vstr_rssicalfrqg)[] =
	"rssi_cal_freq_grp_2g=0x%x0x%x0x%x0x%x0x%x0x%x0x%x";
static const char BCMATTACHDATA(vstr_rssidelta2g)[] =
	"rssi_delta_2gb%d=%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d";
static const char BCMATTACHDATA(vstr_rssidelta5g)[] =
	"rssi_delta_5g%s=%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d";

uint8 patch_pair = 0;

/* For dongle HW, accept partial calibration parameters */
#if defined(BCMSDIODEV) || defined(BCMUSBDEV) || defined(BCMDONGLEHOST)
#define BCMDONGLECASE(n) case n:
#else
#define BCMDONGLECASE(n)
#endif

#ifdef BCM_BOOTLOADER
/* The format of the PMUREGS OTP Tuple ->
 * 1 byte -> Lower 5 bits has the address of the register
 *                 Higher 3 bits has the mode of the register like
 *                 PLL, ChipCtrl, RegCtrl, UpDwn or Dependency mask
 * 4 bytes -> Value of the register to be updated.
 */
#define PMUREGS_MODE_MASK	0xE0
#define PMUREGS_MODE_SHIFT	5
#define PMUREGS_ADDR_MASK	0x1F
#define PMUREGS_TPL_SIZE	5

enum {
	PMU_PLLREG_MODE,
	PMU_CCREG_MODE,
	PMU_VOLTREG_MODE,
	PMU_RES_TIME_MODE,
	PMU_RESDEPEND_MODE
};

#define USBREGS_TPL_SIZE	5
enum {
	USB_DEV_CTRL_REG,
	HSIC_PHY_CTRL1_REG,
	HSIC_PHY_CTRL2_REG
};

#define USBRDY_DLY_TYPE	0x8000	/* Bit indicating if the byte is pre or post delay value */
#define USBRDY_DLY_MASK	0x7FFF	/* Bits indicating the amount of delay */
#define USBRDY_MAXOTP_SIZE	5	/* Max size of the OTP parameter */

#endif /* BCM_BOOTLOADER */

static uint
BCMATTACHFN(get_max_cis_size)(si_t *sih)
{
	uint max_cis_size;
	void *oh;

	max_cis_size = (sih && sih->ccrev >= 49) ? CIS_SIZE_12K : CIS_SIZE;
	if (sih && (oh = otp_init(sih)) != NULL) {
		max_cis_size -= otp_avsbitslen(oh);
	}
	return max_cis_size;
}

#ifndef BCM_BOOTLOADER
static uint32
BCMATTACHFN(srom_data2value)(uint8 *p, uint8 len)
{
	uint8 pos = 0;
	uint32 value = 0;

	ASSERT(len <= 4);

	while (pos < len) {
		value += (p[pos] << (pos * 8));
		pos++;
	}

	return value;
}
#endif /* BCM_BOOTLOADER */

/**
 * Both SROM and OTP contain variables in 'CIS' format, whereas the rest of the firmware works with
 * 'variable/value' string pairs.
 */
int
BCMATTACHFN(srom_parsecis)(si_t *sih, osl_t *osh, uint8 *pcis[], uint ciscnt, char **vars,
	uint *count)
{
	char eabuf[32];
	char eabuf2[32];
	char *base;
	varbuf_t b;
	uint8 *cis, tup, tlen, sromrev = 1;
	uint i;
	uint16 j;
#ifndef BCM_BOOTLOADER
	bool ag_init = FALSE;
#endif
	uint32 w32;
	uint funcid;
	uint cisnum;
	int32 boardnum;
	int err;
	bool standard_cis;
	uint max_cis_size;
	uint var_cis_size = 0;

	ASSERT(count != NULL);

	if (vars == NULL) {
		ASSERT(0);	/* crash debug images for investigation */
		return BCME_BADARG;
	}

	boardnum = -1;

	/* freed in same function */
	max_cis_size = get_max_cis_size(sih);
	var_cis_size = *count + ((max_cis_size + 2u) * ciscnt);

	ASSERT(var_cis_size <= MAXSZ_NVRAM_VARS);

	base = MALLOC_NOPERSIST(osh, var_cis_size);
	ASSERT(base != NULL);
	if (!base)
		return -2;

	varbuf_init(&b, base, var_cis_size);
	bzero(base, var_cis_size);
	/* Append from vars if there's already something inside */
	if (*vars && **vars && (*count >= 3)) {
		/* back off \0 at the end, leaving only one \0 for the last param */
		while (((*vars)[(*count)-1] == '\0') && ((*vars)[(*count)-2] == '\0'))
			(*count)--;

		bcopy(*vars, base, *count);
		b.buf += *count;
	}
	eabuf[0] = '\0';
	eabuf2[0] = '\0';
	for (cisnum = 0; cisnum < ciscnt; cisnum++) {
		cis = *pcis++;
		i = 0;
		funcid = 0;
		standard_cis = TRUE;
		do {
			if (standard_cis) {
				tup = cis[i++];
				if (tup == CISTPL_NULL || tup == CISTPL_END)
					tlen = 0;
				else
					tlen = cis[i++];
			} else {
				if (cis[i] == CISTPL_NULL || cis[i] == CISTPL_END) {
					tlen = 0;
					tup = cis[i];
				} else {
					tlen = cis[i];
					tup = CISTPL_BRCM_HNBU;
				}
				++i;
			}
			if ((i + tlen) >= max_cis_size)
				break;

			switch (tup) {
			case CISTPL_VERS_1:
				/* assume the strings are good if the version field checks out */
				if (((cis[i + 1] << 8) + cis[i]) >= 0x0008) {
					varbuf_append(&b, vstr_manf, &cis[i + 2]);
					varbuf_append(&b, vstr_productname,
					              &cis[i + 3 + strlen((char *)&cis[i + 2])]);
					break;
				}

			case CISTPL_MANFID:
				varbuf_append(&b, vstr_manfid, (cis[i + 1] << 8) + cis[i]);
				varbuf_append(&b, vstr_prodid, (cis[i + 3] << 8) + cis[i + 2]);
				break;

			case CISTPL_FUNCID:
				funcid = cis[i];
				break;

			case CISTPL_FUNCE:
				switch (funcid) {
				case CISTPL_FID_SDIO:
#ifdef BCMSDIO
					if (cis[i] == 0) {
						uint8 spd = cis[i + 3];
						static int lbase[] = {
							-1, 10, 12, 13, 15, 20, 25, 30,
							35, 40, 45, 50, 55, 60, 70, 80
						};
						static int mult[] = {
							10, 100, 1000, 10000,
							-1, -1, -1, -1
						};
						ASSERT((mult[spd & 0x7] != -1) &&
						       (lbase[(spd >> 3) & 0x0f]));
						varbuf_append(&b, vstr_sdmaxblk[0],
						              (cis[i + 2] << 8) + cis[i + 1]);
						varbuf_append(&b, vstr_sdmaxspeed,
						              (mult[spd & 0x7] *
						               lbase[(spd >> 3) & 0x0f]));
					} else if (cis[i] == 1) {
						varbuf_append(&b, vstr_sdmaxblk[cisnum],
						              (cis[i + 13] << 8) | cis[i + 12]);
					}
#endif /* BCMSDIO */
					funcid = 0;
					break;
				default:
					/* set macaddr if HNBU_MACADDR not seen yet */
					if (eabuf[0] == '\0' && cis[i] == LAN_NID &&
						!(ETHER_ISNULLADDR(&cis[i + 2])) &&
						!(ETHER_ISMULTI(&cis[i + 2]))) {
						ASSERT(cis[i + 1] == ETHER_ADDR_LEN);
						bcm_ether_ntoa((struct ether_addr *)&cis[i + 2],
						               eabuf);

						/* set boardnum if HNBU_BOARDNUM not seen yet */
						if (boardnum == -1)
							boardnum = (cis[i + 6] << 8) + cis[i + 7];
					}
					break;
				}
				break;

			case CISTPL_CFTABLE:
				varbuf_append(&b, vstr_regwindowsz, (cis[i + 7] << 8) | cis[i + 6]);
				break;

			case CISTPL_BRCM_HNBU:
				switch (cis[i]) {
				case HNBU_SROMREV:
					sromrev = cis[i + 1];
					varbuf_append(&b, vstr_sromrev, sromrev);
					break;

				case HNBU_XTALFREQ:
					varbuf_append(&b, vstr_xtalfreq,
					              (cis[i + 4] << 24) |
					              (cis[i + 3] << 16) |
					              (cis[i + 2] << 8) |
					              cis[i + 1]);
					break;

				case HNBU_CHIPID:
					varbuf_append(&b, vstr_vendid, (cis[i + 2] << 8) +
					              cis[i + 1]);
					varbuf_append(&b, vstr_devid, (cis[i + 4] << 8) +
					              cis[i + 3]);
					if (tlen >= 7) {
						varbuf_append(&b, vstr_chiprev,
						              (cis[i + 6] << 8) + cis[i + 5]);
					}
					if (tlen >= 9) {
						varbuf_append(&b, vstr_subvendid,
						              (cis[i + 8] << 8) + cis[i + 7]);
					}
					if (tlen >= 11) {
						varbuf_append(&b, vstr_subdevid,
						              (cis[i + 10] << 8) + cis[i + 9]);
						/* subdevid doubles for boardtype */
						varbuf_append(&b, vstr_boardtype,
						              (cis[i + 10] << 8) + cis[i + 9]);
					}
					break;

				case HNBU_BOARDNUM:
					boardnum = (cis[i + 2] << 8) + cis[i + 1];
					break;

				case HNBU_PATCH: {
					char vstr_paddr[16];
					char vstr_pdata[16];

					/* retrieve the patch pairs
					 * from tlen/6; where 6 is
					 * sizeof(patch addr(2)) +
					 * sizeof(patch data(4)).
					 */
					patch_pair = tlen/6;

					for (j = 0; j < patch_pair; j++) {
						snprintf(vstr_paddr, sizeof(vstr_paddr),
							rstr_paddr, j);
						snprintf(vstr_pdata, sizeof(vstr_pdata),
							rstr_pdata, j);

						varbuf_append(&b, vstr_paddr,
							(cis[i + (j*6) + 2] << 8) |
							cis[i + (j*6) + 1]);

						varbuf_append(&b, vstr_pdata,
							(cis[i + (j*6) + 6] << 24) |
							(cis[i + (j*6) + 5] << 16) |
							(cis[i + (j*6) + 4] << 8) |
							cis[i + (j*6) + 3]);
					}
					break;
				}

				case HNBU_BOARDREV:
					if (tlen == 2)
						varbuf_append(&b, vstr_boardrev, cis[i + 1]);
					else
						varbuf_append(&b, vstr_boardrev,
							(cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_BOARDFLAGS:
					w32 = (cis[i + 2] << 8) + cis[i + 1];
					if (tlen >= 5)
						w32 |= ((cis[i + 4] << 24) + (cis[i + 3] << 16));
					varbuf_append(&b, vstr_boardflags, w32);

					if (tlen >= 7) {
						w32 = (cis[i + 6] << 8) + cis[i + 5];
						if (tlen >= 9)
							w32 |= ((cis[i + 8] << 24) +
								(cis[i + 7] << 16));
						varbuf_append(&b, vstr_boardflags2, w32);
					}
					if (tlen >= 11) {
						w32 = (cis[i + 10] << 8) + cis[i + 9];
						if (tlen >= 13)
							w32 |= ((cis[i + 12] << 24) +
								(cis[i + 11] << 16));
						varbuf_append(&b, vstr_boardflags3, w32);
					}
					if (tlen >= 15) {
						w32 = (cis[i + 14] << 8) + cis[i + 13];
						if (tlen >= 17)
							w32 |= ((cis[i + 16] << 24) +
								(cis[i + 15] << 16));
						varbuf_append(&b, vstr_boardflags4, w32);
					}
					if (tlen >= 19) {
						w32 = (cis[i + 18] << 8) + cis[i + 17];
						if (tlen >= 21)
							w32 |= ((cis[i + 20] << 24) +
								(cis[i + 19] << 16));
						varbuf_append(&b, vstr_boardflags5, w32);
					}
					break;

				case HNBU_USBFS:
					varbuf_append(&b, vstr_usbfs, cis[i + 1]);
					break;

				case HNBU_BOARDTYPE:
					varbuf_append(&b, vstr_boardtype,
					              (cis[i + 2] << 8) + cis[i + 1]);
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
					standard_cis = FALSE;
					break;

				case HNBU_USBEPNUM:
					varbuf_append(&b, vstr_usbepnum,
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_PATCH_AUTOINC: {
					char vstr_paddr[16];
					char vstr_pdata[16];
					uint32 addr_inc;
					uint8 pcnt;

					addr_inc = (cis[i + 4] << 24) |
						(cis[i + 3] << 16) |
						(cis[i + 2] << 8) |
						(cis[i + 1]);

					pcnt = (tlen - 5)/4;
					for (j = 0; j < pcnt; j++) {
						snprintf(vstr_paddr, sizeof(vstr_paddr),
							rstr_paddr, j + patch_pair);
						snprintf(vstr_pdata, sizeof(vstr_pdata),
							rstr_pdata, j + patch_pair);

						varbuf_append(&b, vstr_paddr, addr_inc);
						varbuf_append(&b, vstr_pdata,
							(cis[i + (j*4) + 8] << 24) |
							(cis[i + (j*4) + 7] << 16) |
							(cis[i + (j*4) + 6] << 8) |
							cis[i + (j*4) + 5]);
						addr_inc += 4;
					}
					patch_pair += pcnt;
					break;
				}
				case HNBU_PATCH2: {
					char vstr_paddr[16];
					char vstr_pdata[16];

					/* retrieve the patch pairs
					 * from tlen/8; where 8 is
					 * sizeof(patch addr(4)) +
					 * sizeof(patch data(4)).
					 */
					patch_pair = tlen/8;

					for (j = 0; j < patch_pair; j++) {
						snprintf(vstr_paddr, sizeof(vstr_paddr),
							rstr_paddr, j);
						snprintf(vstr_pdata, sizeof(vstr_pdata),
							rstr_pdata, j);

						varbuf_append(&b, vstr_paddr,
							(cis[i + (j*8) + 4] << 24) |
							(cis[i + (j*8) + 3] << 16) |
							(cis[i + (j*8) + 2] << 8) |
							cis[i + (j*8) + 1]);

						varbuf_append(&b, vstr_pdata,
							(cis[i + (j*8) + 8] << 24) |
							(cis[i + (j*8) + 7] << 16) |
							(cis[i + (j*8) + 6] << 8) |
							cis[i + (j*8) + 5]);
					}
					break;
				}
				case HNBU_PATCH_AUTOINC8: {
					char vstr_paddr[16];
					char vstr_pdatah[16];
					char vstr_pdatal[16];
					uint32 addr_inc;
					uint8 pcnt;

					addr_inc = (cis[i + 4] << 24) |
						(cis[i + 3] << 16) |
						(cis[i + 2] << 8) |
						(cis[i + 1]);

					pcnt = (tlen - 5)/8;
					for (j = 0; j < pcnt; j++) {
						snprintf(vstr_paddr, sizeof(vstr_paddr),
							rstr_paddr, j + patch_pair);
						snprintf(vstr_pdatah, sizeof(vstr_pdatah),
							rstr_pdatah, j + patch_pair);
						snprintf(vstr_pdatal, sizeof(vstr_pdatal),
							rstr_pdatal, j + patch_pair);

						varbuf_append(&b, vstr_paddr, addr_inc);
						varbuf_append(&b, vstr_pdatal,
							(cis[i + (j*8) + 8] << 24) |
							(cis[i + (j*8) + 7] << 16) |
							(cis[i + (j*8) + 6] << 8) |
							cis[i + (j*8) + 5]);
						varbuf_append(&b, vstr_pdatah,
							(cis[i + (j*8) + 12] << 24) |
							(cis[i + (j*8) + 11] << 16) |
							(cis[i + (j*8) + 10] << 8) |
							cis[i + (j*8) + 9]);
						addr_inc += 8;
					}
					patch_pair += pcnt;
					break;
				}
				case HNBU_PATCH8: {
					char vstr_paddr[16];
					char vstr_pdatah[16];
					char vstr_pdatal[16];

					/* retrieve the patch pairs
					 * from tlen/8; where 8 is
					 * sizeof(patch addr(4)) +
					 * sizeof(patch data(4)).
					 */
					patch_pair = tlen/12;

					for (j = 0; j < patch_pair; j++) {
						snprintf(vstr_paddr, sizeof(vstr_paddr),
							rstr_paddr, j);
						snprintf(vstr_pdatah, sizeof(vstr_pdatah),
							rstr_pdatah, j);
						snprintf(vstr_pdatal, sizeof(vstr_pdatal),
							rstr_pdatal, j);

						varbuf_append(&b, vstr_paddr,
							(cis[i + (j*12) + 4] << 24) |
							(cis[i + (j*12) + 3] << 16) |
							(cis[i + (j*12) + 2] << 8) |
							cis[i + (j*12) + 1]);

						varbuf_append(&b, vstr_pdatal,
							(cis[i + (j*12) + 8] << 24) |
							(cis[i + (j*12) + 7] << 16) |
							(cis[i + (j*12) + 6] << 8) |
							cis[i + (j*12) + 5]);

						varbuf_append(&b, vstr_pdatah,
							(cis[i + (j*12) + 12] << 24) |
							(cis[i + (j*12) + 11] << 16) |
							(cis[i + (j*12) + 10] << 8) |
							cis[i + (j*12) + 9]);
					}
					break;
				}
				case HNBU_USBFLAGS:
					varbuf_append(&b, vstr_usbflags,
					              (cis[i + 4] << 24) |
					              (cis[i + 3] << 16) |
					              (cis[i + 2] << 8) |
					              cis[i + 1]);
					break;
#ifdef BCM_BOOTLOADER
				case HNBU_MDIOEX_REGLIST:
				case HNBU_MDIO_REGLIST: {
					/* Format: addr (8 bits) | val (16 bits) */
					const uint8 msize = 3;
					char mdiostr[24];
					const char *mdiodesc;
					uint8 *st;

					mdiodesc = (cis[i] == HNBU_MDIO_REGLIST) ?
						vstr_mdio : vstr_mdioex;

					ASSERT(((tlen - 1) % msize) == 0);

					st = &cis[i + 1]; /* start of reg list */
					for (j = 0; j < (tlen - 1); j += msize, st += msize) {
						snprintf(mdiostr, sizeof(mdiostr),
							mdiodesc, st[0]);
						varbuf_append(&b, mdiostr, (st[2] << 8) | st[1]);
					}
					break;
				}
				case HNBU_BRMIN:
					varbuf_append(&b, vstr_brmin,
					              (cis[i + 4] << 24) |
					              (cis[i + 3] << 16) |
					              (cis[i + 2] << 8) |
					              cis[i + 1]);
					break;

				case HNBU_BRMAX:
					varbuf_append(&b, vstr_brmax,
					              (cis[i + 4] << 24) |
					              (cis[i + 3] << 16) |
					              (cis[i + 2] << 8) |
					              cis[i + 1]);
					break;
#endif /* BCM_BOOTLOADER */

				case HNBU_RDLID:
					varbuf_append(&b, vstr_rdlid,
					              (cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_GCI_CCR: {
					/* format:
					 * |0x80|	<== brcm
					 * |len|	<== variable, multiple of 5
					 * |tup|	<== tupletype
					 * |ccreg_ix0|	<== ix of ccreg [1byte]
					 * |ccreg_val0|	<== corr value [4bytes]
					 *	---
					 * Multiple registers are possible. for eg: we
					 *	can specify reg_ix3val3 and reg_ix5val5, etc
					 */
					char vstr_gci_ccreg_entry[16];
					uint8 num_entries = 0;

					/* retrieve the index-value pairs
					 * from tlen/5; where 5 is
					 * sizeof(ccreg_ix(1)) +
					 * sizeof(ccreg_val(4)).
					 */
					num_entries = tlen/5;

					for (j = 0; j < num_entries; j++) {
						snprintf(vstr_gci_ccreg_entry,
							sizeof(vstr_gci_ccreg_entry),
							rstr_gci_ccreg_entry,
							cis[i + (j*5) + 1]);

						varbuf_append(&b, vstr_gci_ccreg_entry,
							(cis[i + (j*5) + 5] << 24) |
							(cis[i + (j*5) + 4] << 16) |
							(cis[i + (j*5) + 3] << 8) |
							cis[i + (j*5) + 2]);
					}
					break;
				}

#ifdef BCM_BOOTLOADER
				case HNBU_RDLRNDIS:
					varbuf_append(&b, vstr_rdlrndis, cis[i + 1]);
					break;

				case HNBU_RDLRWU:
					varbuf_append(&b, vstr_rdlrwu, cis[i + 1]);
					break;

				case HNBU_RDLSN:
					if (tlen >= 5)
						varbuf_append(&b, vstr_rdlsn,
						              (cis[i + 4] << 24) |
						              (cis[i + 3] << 16) |
						              (cis[i + 2] << 8) |
						              cis[i + 1]);
					else
						varbuf_append(&b, vstr_rdlsn,
						              (cis[i + 2] << 8) |
						              cis[i + 1]);
					break;

				case HNBU_PMUREGS: {
					uint8 offset = 1, mode_addr, mode, addr;
					const char *fmt;

					do {
						mode_addr = cis[i+offset];

						mode = (mode_addr & PMUREGS_MODE_MASK)
							>> PMUREGS_MODE_SHIFT;
						addr = mode_addr & PMUREGS_ADDR_MASK;

						switch (mode) {
						case PMU_PLLREG_MODE:
							fmt = vstr_pllreg;
							break;
						case PMU_CCREG_MODE:
							fmt = vstr_ccreg;
							break;
						case PMU_VOLTREG_MODE:
							fmt = vstr_regctrl;
							break;
						case PMU_RES_TIME_MODE:
							fmt = vstr_time;
							break;
						case PMU_RESDEPEND_MODE:
							fmt = vstr_depreg;
							break;
						default:
							fmt = NULL;
							break;
						}

						if (fmt != NULL) {
							varbuf_append(&b, fmt, addr,
								(cis[i + offset + 4] << 24) |
								(cis[i + offset + 3] << 16) |
								(cis[i + offset + 2] << 8) |
								cis[i + offset + 1]);
						}

						offset += PMUREGS_TPL_SIZE;
					} while (offset < tlen);
					break;
				}

				case HNBU_USBREGS: {
					uint8 offset = 1, usb_reg;
					const char *fmt;

					do {
						usb_reg = cis[i+offset];

						switch (usb_reg) {
						case USB_DEV_CTRL_REG:
							fmt = vstr_usbdevctrl;
							break;
						case HSIC_PHY_CTRL1_REG:
							fmt = vstr_hsicphyctrl1;
							break;
						case HSIC_PHY_CTRL2_REG:
							fmt = vstr_hsicphyctrl2;
							break;
						default:
							fmt = NULL;
							break;
						}

						if (fmt != NULL) {
							varbuf_append(&b, fmt,
								(cis[i + offset + 4] << 24) |
								(cis[i + offset + 3] << 16) |
								(cis[i + offset + 2] << 8) |
								cis[i + offset + 1]);
						}

						offset += USBREGS_TPL_SIZE;
					} while (offset < tlen);
					break;
				}

				case HNBU_USBRDY:
					/* The first byte of this tuple indicate if the host
					 * needs to be informed about the readiness of
					 * the HSIC/USB for enumeration on which GPIO should
					 * the device assert this event.
					 */
					varbuf_append(&b, vstr_usbrdy, cis[i + 1]);

					/* The following fields in this OTP are optional.
					 * The remaining bytes will indicate the delay required
					 * before and/or after the ch_init(). The delay is defined
					 * using 16-bits of this the MSB(bit15 of 15:0) will be
					 * used indicate if the parameter is for Pre or Post delay.
					 */
					for (j = 2; j < USBRDY_MAXOTP_SIZE && j < tlen;
						j += 2) {
						uint16 usb_delay;

						usb_delay = cis[i + j] | (cis[i + j + 1] << 8);

						/* The bit-15 of the delay field will indicate the
						 * type of delay (pre or post).
						 */
						if (usb_delay & USBRDY_DLY_TYPE) {
							varbuf_append(&b, vstr_usbpostdly,
							(usb_delay & USBRDY_DLY_MASK));
						} else {
							varbuf_append(&b, vstr_usbpredly,
							(usb_delay & USBRDY_DLY_MASK));
						}
					}
					break;

				case HNBU_BLDR_TIMEOUT:
					/* The Delay after USBConnect for timeout till dongle
					 * receives get_descriptor request.
					 */
					varbuf_append(&b, vstr_bldr_reset_timeout,
						(cis[i + 1] | (cis[i + 2] << 8)));
					break;

				case HNBU_MUXENAB:
					varbuf_append(&b, vstr_muxenab, cis[i + 1]);
					break;
				case HNBU_PUBKEY: {
					/* The public key is in binary format in OTP,
					 * convert to string format before appending
					 * buffer string.
					 *  public key(12 bytes) + crc (1byte) = 129
					 */
					unsigned char a[300];
					int k;

					for (k = 1, j = 0; k < 129; k++)
						j += snprintf((char *)(a + j),
							sizeof(a) - j,
							"%02x", cis[i + k]);

					a[256] = 0;

					varbuf_append(&b, vstr_pubkey, a);
					break;
				}
#else
				case HNBU_AA:
					varbuf_append(&b, vstr_aa2g, cis[i + 1]);
					if (tlen >= 3)
						varbuf_append(&b, vstr_aa5g, cis[i + 2]);
					break;

				case HNBU_AG:
					varbuf_append(&b, vstr_ag, 0, cis[i + 1]);
					if (tlen >= 3)
						varbuf_append(&b, vstr_ag, 1, cis[i + 2]);
					if (tlen >= 4)
						varbuf_append(&b, vstr_ag, 2, cis[i + 3]);
					if (tlen >= 5)
						varbuf_append(&b, vstr_ag, 3, cis[i + 4]);
					ag_init = TRUE;
					break;

				case HNBU_ANT5G:
					varbuf_append(&b, vstr_aa5g, cis[i + 1]);
					varbuf_append(&b, vstr_ag, 1, cis[i + 2]);
					break;

				case HNBU_CC:
					ASSERT(sromrev == 1);
					varbuf_append(&b, vstr_cc, cis[i + 1]);
					break;

				case HNBU_PAPARMS: {
					uint8 pa0_lo_offset = 0;
					switch (tlen) {
					case 2:
						ASSERT(sromrev == 1);
						varbuf_append(&b, vstr_pa0maxpwr, cis[i + 1]);
						break;
					/* case 16:
						ASSERT(sromrev >= 11);
						for (j = 0; j < 3; j++) {
						varbuf_append(&b, vstr_pa0b_lo[j],
								(cis[i + (j * 2) + 11] << 8) +
								cis[i + (j * 2) + 10]);
						}
						 FALLTHROUGH
					*/
					case 10:
					case 16:
						ASSERT(sromrev >= 2);
						varbuf_append(&b, vstr_opo, cis[i + 9]);
						if (tlen >= 13 && pa0_lo_offset == 0)
							pa0_lo_offset = 9;
						/* FALLTHROUGH */
					case 9:
					case 15:
						varbuf_append(&b, vstr_pa0maxpwr, cis[i + 8]);
						if (tlen >= 13 && pa0_lo_offset == 0)
							pa0_lo_offset = 8;
						/* FALLTHROUGH */
					BCMDONGLECASE(8)
					BCMDONGLECASE(14)
						varbuf_append(&b, vstr_pa0itssit, cis[i + 7]);
						varbuf_append(&b, vstr_maxp2ga, 0, cis[i + 7]);
						if (tlen >= 13 && pa0_lo_offset == 0)
							pa0_lo_offset = 7;
						/* FALLTHROUGH */
					BCMDONGLECASE(7)
					BCMDONGLECASE(13)
					        for (j = 0; j < 3; j++) {
							varbuf_append(&b, vstr_pa0b[j],
							              (cis[i + (j * 2) + 2] << 8) +
							              cis[i + (j * 2) + 1]);
						}
						if (tlen >= 13 && pa0_lo_offset == 0)
							pa0_lo_offset = 6;

						if (tlen >= 13 && pa0_lo_offset != 0) {
							for (j = 0; j < 3; j++) {
								varbuf_append(&b, vstr_pa0b_lo[j],
								 (cis[pa0_lo_offset+i+(j*2)+2]<<8)+
								 cis[pa0_lo_offset+i+(j*2)+1]);
							}
						}
						break;
					default:
						ASSERT((tlen == 2) || (tlen == 9) || (tlen == 10) ||
							(tlen == 15) || (tlen == 16));
						break;
					}
					break;
				}
				case HNBU_PAPARMS5G:
					ASSERT((sromrev == 2) || (sromrev == 3));
					switch (tlen) {
					case 23:
						varbuf_append(&b, vstr_pa1himaxpwr, cis[i + 22]);
						varbuf_append(&b, vstr_pa1lomaxpwr, cis[i + 21]);
						varbuf_append(&b, vstr_pa1maxpwr, cis[i + 20]);
						/* FALLTHROUGH */
					case 20:
						varbuf_append(&b, vstr_pa1itssit, cis[i + 19]);
						/* FALLTHROUGH */
					case 19:
						for (j = 0; j < 3; j++) {
							varbuf_append(&b, vstr_pa1b[j],
							              (cis[i + (j * 2) + 2] << 8) +
							              cis[i + (j * 2) + 1]);
						}
						for (j = 3; j < 6; j++) {
							varbuf_append(&b, vstr_pa1lob[j - 3],
							              (cis[i + (j * 2) + 2] << 8) +
							              cis[i + (j * 2) + 1]);
						}
						for (j = 6; j < 9; j++) {
							varbuf_append(&b, vstr_pa1hib[j - 6],
							              (cis[i + (j * 2) + 2] << 8) +
							              cis[i + (j * 2) + 1]);
						}
						break;
					default:
						ASSERT((tlen == 19) ||
						       (tlen == 20) || (tlen == 23));
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

				case HNBU_CCODE:
					ASSERT(sromrev > 1);
					if ((cis[i + 1] == 0) || (cis[i + 2] == 0))
						varbuf_append(&b, vstr_noccode);
					else
						varbuf_append(&b, vstr_ccode,
						              cis[i + 1], cis[i + 2]);
					varbuf_append(&b, vstr_cctl, cis[i + 3]);
					break;

				case HNBU_CCKPO:
					ASSERT(sromrev > 2);
					varbuf_append(&b, vstr_cckpo,
					              (cis[i + 2] << 8) | cis[i + 1]);
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
					varbuf_append(&b, vstr_wpsgpio, cis[i + 1]);
					if (tlen >= 3)
						varbuf_append(&b, vstr_wpsled, cis[i + 2]);
					break;

				case HNBU_RSSISMBXA2G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rssismf2g, cis[i + 1] & 0xf);
					varbuf_append(&b, vstr_rssismc2g, (cis[i + 1] >> 4) & 0xf);
					varbuf_append(&b, vstr_rssisav2g, cis[i + 2] & 0x7);
					varbuf_append(&b, vstr_bxa2g, (cis[i + 2] >> 3) & 0x3);
					break;

				case HNBU_RSSISMBXA5G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rssismf5g, cis[i + 1] & 0xf);
					varbuf_append(&b, vstr_rssismc5g, (cis[i + 1] >> 4) & 0xf);
					varbuf_append(&b, vstr_rssisav5g, cis[i + 2] & 0x7);
					varbuf_append(&b, vstr_bxa5g, (cis[i + 2] >> 3) & 0x3);
					break;

				case HNBU_TRI2G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_tri2g, cis[i + 1]);
					break;

				case HNBU_TRI5G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_tri5gl, cis[i + 1]);
					varbuf_append(&b, vstr_tri5g, cis[i + 2]);
					varbuf_append(&b, vstr_tri5gh, cis[i + 3]);
					break;

				case HNBU_RXPO2G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rxpo2g, cis[i + 1]);
					break;

				case HNBU_RXPO5G:
					ASSERT(sromrev == 3);
					varbuf_append(&b, vstr_rxpo5g, cis[i + 1]);
					break;

				case HNBU_MACADDR:
					if (!(ETHER_ISNULLADDR(&cis[i+1])) &&
					    !(ETHER_ISMULTI(&cis[i+1]))) {
						bcm_ether_ntoa((struct ether_addr *)&cis[i + 1],
						               eabuf);

						/* set boardnum if HNBU_BOARDNUM not seen yet */
						if (boardnum == -1)
							boardnum = (cis[i + 5] << 8) + cis[i + 6];
					}
					break;

				case HNBU_CHAINSWITCH:
					varbuf_append(&b, vstr_txchain, cis[i + 1]);
					varbuf_append(&b, vstr_rxchain, cis[i + 2]);
					varbuf_append(&b, vstr_antswitch,
					      (cis[i + 4] << 8) + cis[i + 3]);
					break;

				case HNBU_ELNA2G:
					varbuf_append(&b, vstr_elna2g, cis[i + 1]);
					break;

				case HNBU_ELNA5G:
					varbuf_append(&b, vstr_elna5g, cis[i + 1]);
					break;

				case HNBU_REGREV:
					varbuf_append(&b, vstr_regrev,
						srom_data2value(&cis[i + 1], tlen - 1));
					break;

				case HNBU_FEM: {
					uint16 fem = (cis[i + 2] << 8) + cis[i + 1];
					varbuf_append(&b, vstr_antswctl2g, (fem &
						SROM8_FEM_ANTSWLUT_MASK) >>
						SROM8_FEM_ANTSWLUT_SHIFT);
					varbuf_append(&b, vstr_triso2g, (fem &
						SROM8_FEM_TR_ISO_MASK) >>
						SROM8_FEM_TR_ISO_SHIFT);
					varbuf_append(&b, vstr_pdetrange2g, (fem &
						SROM8_FEM_PDET_RANGE_MASK) >>
						SROM8_FEM_PDET_RANGE_SHIFT);
					varbuf_append(&b, vstr_extpagain2g, (fem &
						SROM8_FEM_EXTPA_GAIN_MASK) >>
						SROM8_FEM_EXTPA_GAIN_SHIFT);
					varbuf_append(&b, vstr_tssipos2g, (fem &
						SROM8_FEM_TSSIPOS_MASK) >>
						SROM8_FEM_TSSIPOS_SHIFT);
					if (tlen < 5) break;

					fem = (cis[i + 4] << 8) + cis[i + 3];
					varbuf_append(&b, vstr_antswctl5g, (fem &
						SROM8_FEM_ANTSWLUT_MASK) >>
						SROM8_FEM_ANTSWLUT_SHIFT);
					varbuf_append(&b, vstr_triso5g, (fem &
						SROM8_FEM_TR_ISO_MASK) >>
						SROM8_FEM_TR_ISO_SHIFT);
					varbuf_append(&b, vstr_pdetrange5g, (fem &
						SROM8_FEM_PDET_RANGE_MASK) >>
						SROM8_FEM_PDET_RANGE_SHIFT);
					varbuf_append(&b, vstr_extpagain5g, (fem &
						SROM8_FEM_EXTPA_GAIN_MASK) >>
						SROM8_FEM_EXTPA_GAIN_SHIFT);
					varbuf_append(&b, vstr_tssipos5g, (fem &
						SROM8_FEM_TSSIPOS_MASK) >>
						SROM8_FEM_TSSIPOS_SHIFT);
					break;
				}

				case HNBU_PAPARMS_C0:
					varbuf_append(&b, vstr_maxp2ga, 0, cis[i + 1]);
					varbuf_append(&b, vstr_itt2ga0, cis[i + 2]);
					varbuf_append(&b, vstr_pa, 2, 0, 0,
						(cis[i + 4] << 8) + cis[i + 3]);
					varbuf_append(&b, vstr_pa, 2, 1, 0,
						(cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_pa, 2, 2, 0,
						(cis[i + 8] << 8) + cis[i + 7]);
					if (tlen < 31) break;

					varbuf_append(&b, vstr_maxp5ga0, cis[i + 9]);
					varbuf_append(&b, vstr_itt5ga0, cis[i + 10]);
					varbuf_append(&b, vstr_maxp5gha0, cis[i + 11]);
					varbuf_append(&b, vstr_maxp5gla0, cis[i + 12]);
					varbuf_append(&b, vstr_pa, 5, 0, 0,
						(cis[i + 14] << 8) + cis[i + 13]);
					varbuf_append(&b, vstr_pa, 5, 1, 0,
						(cis[i + 16] << 8) + cis[i + 15]);
					varbuf_append(&b, vstr_pa, 5, 2, 0,
						(cis[i + 18] << 8) + cis[i + 17]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 0, 0,
						(cis[i + 20] << 8) + cis[i + 19]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 1, 0,
						(cis[i + 22] << 8) + cis[i + 21]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 2, 0,
						(cis[i + 24] << 8) + cis[i + 23]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 0, 0,
						(cis[i + 26] << 8) + cis[i + 25]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 1, 0,
						(cis[i + 28] << 8) + cis[i + 27]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 2, 0,
						(cis[i + 30] << 8) + cis[i + 29]);
					break;

				case HNBU_PAPARMS_C1:
					varbuf_append(&b, vstr_maxp2ga, 1, cis[i + 1]);
					varbuf_append(&b, vstr_itt2ga1, cis[i + 2]);
					varbuf_append(&b, vstr_pa, 2, 0, 1,
						(cis[i + 4] << 8) + cis[i + 3]);
					varbuf_append(&b, vstr_pa, 2, 1, 1,
						(cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_pa, 2, 2, 1,
						(cis[i + 8] << 8) + cis[i + 7]);
					if (tlen < 31) break;

					varbuf_append(&b, vstr_maxp5ga1, cis[i + 9]);
					varbuf_append(&b, vstr_itt5ga1, cis[i + 10]);
					varbuf_append(&b, vstr_maxp5gha1, cis[i + 11]);
					varbuf_append(&b, vstr_maxp5gla1, cis[i + 12]);
					varbuf_append(&b, vstr_pa, 5, 0, 1,
						(cis[i + 14] << 8) + cis[i + 13]);
					varbuf_append(&b, vstr_pa, 5, 1, 1,
						(cis[i + 16] << 8) + cis[i + 15]);
					varbuf_append(&b, vstr_pa, 5, 2, 1,
						(cis[i + 18] << 8) + cis[i + 17]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 0, 1,
						(cis[i + 20] << 8) + cis[i + 19]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 1, 1,
						(cis[i + 22] << 8) + cis[i + 21]);
					varbuf_append(&b, vstr_pahl, 5, 'l', 2, 1,
						(cis[i + 24] << 8) + cis[i + 23]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 0, 1,
						(cis[i + 26] << 8) + cis[i + 25]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 1, 1,
						(cis[i + 28] << 8) + cis[i + 27]);
					varbuf_append(&b, vstr_pahl, 5, 'h', 2, 1,
						(cis[i + 30] << 8) + cis[i + 29]);
					break;

				case HNBU_PO_CCKOFDM:
					varbuf_append(&b, vstr_cck2gpo,
						(cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_ofdm2gpo,
						(cis[i + 6] << 24) + (cis[i + 5] << 16) +
						(cis[i + 4] << 8) + cis[i + 3]);
					if (tlen < 19) break;

					varbuf_append(&b, vstr_ofdm5gpo,
						(cis[i + 10] << 24) + (cis[i + 9] << 16) +
						(cis[i + 8] << 8) + cis[i + 7]);
					varbuf_append(&b, vstr_ofdm5glpo,
						(cis[i + 14] << 24) + (cis[i + 13] << 16) +
						(cis[i + 12] << 8) + cis[i + 11]);
					varbuf_append(&b, vstr_ofdm5ghpo,
						(cis[i + 18] << 24) + (cis[i + 17] << 16) +
						(cis[i + 16] << 8) + cis[i + 15]);
					break;

				case HNBU_PO_MCS2G:
					for (j = 0; j <= (tlen/2); j++) {
						varbuf_append(&b, vstr_mcspo, 2, j,
							(cis[i + 2 + 2*j] << 8) + cis[i + 1 + 2*j]);
					}
					break;

				case HNBU_PO_MCS5GM:
					for (j = 0; j <= (tlen/2); j++) {
						varbuf_append(&b, vstr_mcspo, 5, j,
							(cis[i + 2 + 2*j] << 8) + cis[i + 1 + 2*j]);
					}
					break;

				case HNBU_PO_MCS5GLH:
					for (j = 0; j <= (tlen/4); j++) {
						varbuf_append(&b, vstr_mcspohl, 5, 'l', j,
							(cis[i + 2 + 2*j] << 8) + cis[i + 1 + 2*j]);
					}

					for (j = 0; j <= (tlen/4); j++) {
						varbuf_append(&b, vstr_mcspohl, 5, 'h', j,
							(cis[i + ((tlen/2)+2) + 2*j] << 8) +
							cis[i + ((tlen/2)+1) + 2*j]);
					}

					break;

				case HNBU_PO_CDD:
					varbuf_append(&b, vstr_cddpo,
					              (cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_PO_STBC:
					varbuf_append(&b, vstr_stbcpo,
					              (cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_PO_40M:
					varbuf_append(&b, vstr_bw40po,
					              (cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_PO_40MDUP:
					varbuf_append(&b, vstr_bwduppo,
					              (cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_OFDMPO5G:
					varbuf_append(&b, vstr_ofdm5gpo,
						(cis[i + 4] << 24) + (cis[i + 3] << 16) +
						(cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_ofdm5glpo,
						(cis[i + 8] << 24) + (cis[i + 7] << 16) +
						(cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_ofdm5ghpo,
						(cis[i + 12] << 24) + (cis[i + 11] << 16) +
						(cis[i + 10] << 8) + cis[i + 9]);
					break;
				/* Power per rate for SROM V9 */
				case HNBU_CCKBW202GPO:
					varbuf_append(&b, vstr_cckbw202gpo[0],
						((cis[i + 2] << 8) + cis[i + 1]));
					if (tlen > 4)
						varbuf_append(&b, vstr_cckbw202gpo[1],
							((cis[i + 4] << 8) + cis[i + 3]));
					if (tlen > 6)
						varbuf_append(&b, vstr_cckbw202gpo[2],
							((cis[i + 6] << 8) + cis[i + 5]));
					break;

				case HNBU_LEGOFDMBW202GPO:
					varbuf_append(&b, vstr_legofdmbw202gpo[0],
						((cis[i + 4] << 24) + (cis[i + 3] << 16) +
						(cis[i + 2] << 8) + cis[i + 1]));
					if (tlen > 6)  {
						varbuf_append(&b, vstr_legofdmbw202gpo[1],
							((cis[i + 8] << 24) + (cis[i + 7] << 16) +
							(cis[i + 6] << 8) + cis[i + 5]));
					}
					break;

				case HNBU_LEGOFDMBW205GPO:
					for (j = 0; j < 6; j++) {
						if (tlen < (2 + 4 * j))
							break;
						varbuf_append(&b, vstr_legofdmbw205gpo[j],
							((cis[4 * j + i + 4] << 24)
							+ (cis[4 * j + i + 3] << 16)
							+ (cis[4 * j + i + 2] << 8)
							+ cis[4 * j + i + 1]));
					}
					break;

				case HNBU_MCS2GPO:
					for (j = 0; j < 4; j++) {
						if (tlen < (2 + 4 * j))
							break;
						varbuf_append(&b, vstr_mcs2gpo[j],
							((cis[4 * j + i + 4] << 24)
							+ (cis[4 * j + i + 3] << 16)
							+ (cis[4 * j + i + 2] << 8)
							+ cis[4 * j + i + 1]));
					}
					break;

				case HNBU_MCS5GLPO:
					for (j = 0; j < 3; j++) {
						if (tlen < (2 + 4 * j))
							break;
						varbuf_append(&b, vstr_mcs5glpo[j],
							((cis[4 * j + i + 4] << 24)
							+ (cis[4 * j + i + 3] << 16)
							+ (cis[4 * j + i + 2] << 8)
							+ cis[4 * j + i + 1]));
					}
					break;

				case HNBU_MCS5GMPO:
					for (j = 0; j < 3; j++) {
						if (tlen < (2 + 4 * j))
							break;
						varbuf_append(&b, vstr_mcs5gmpo[j],
							((cis[4 * j + i + 4] << 24)
							+ (cis[4 * j + i + 3] << 16)
							+ (cis[4 * j + i + 2] << 8)
							+ cis[4 * j + i + 1]));
					}
					break;

				case HNBU_MCS5GHPO:
					for (j = 0; j < 3; j++) {
						if (tlen < (2 + 4 * j))
							break;
						varbuf_append(&b, vstr_mcs5ghpo[j],
							((cis[4 * j + i + 4] << 24)
							+ (cis[4 * j + i + 3] << 16)
							+ (cis[4 * j + i + 2] << 8)
							+ cis[4 * j + i + 1]));
					}
					break;

				case HNBU_MCS32PO:
					varbuf_append(&b, vstr_mcs32po,
						(cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_LEG40DUPPO:
					varbuf_append(&b, vstr_legofdm40duppo,
						(cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_CUSTOM1:
					varbuf_append(&b, vstr_custom, 1, ((cis[i + 4] << 24) +
						(cis[i + 3] << 16) + (cis[i + 2] << 8) +
						cis[i + 1]));
					break;

#if defined(BCMSDIO) || defined(BCMCCISSR3)
				case HNBU_SROM3SWRGN:
					if (tlen >= 73) {
						uint16 srom[35];
						uint8 srev = cis[i + 1 + 70];
						ASSERT(srev == 3);
						/* make tuple value 16-bit aligned and parse it */
						bcopy(&cis[i + 1], srom, sizeof(srom));
						_initvars_srom_pci(srev, srom, SROM3_SWRGN_OFF, &b);
						/* 2.4G antenna gain is included in SROM */
						ag_init = TRUE;
						/* Ethernet MAC address is included in SROM */
						eabuf[0] = 0;
						/* why boardnum is not -1? */
						boardnum = -1;
					}
					/* create extra variables */
					if (tlen >= 75)
						varbuf_append(&b, vstr_vendid,
						              (cis[i + 1 + 73] << 8) +
						              cis[i + 1 + 72]);
					if (tlen >= 77)
						varbuf_append(&b, vstr_devid,
						              (cis[i + 1 + 75] << 8) +
						              cis[i + 1 + 74]);
					if (tlen >= 79)
						varbuf_append(&b, vstr_xtalfreq,
						              (cis[i + 1 + 77] << 8) +
						              cis[i + 1 + 76]);
					break;
#endif	/* BCMSDIO || BCMCCISSR3 */

				case HNBU_CCKFILTTYPE:
					varbuf_append(&b, vstr_cckdigfilttype,
						(cis[i + 1]));
					break;

				case HNBU_TEMPTHRESH:
					varbuf_append(&b, vstr_tempthresh,
						(cis[i + 1]));
					/* period in msb nibble */
					varbuf_append(&b, vstr_temps_period,
						(cis[i + 2] & SROM11_TEMPS_PERIOD_MASK) >>
						SROM11_TEMPS_PERIOD_SHIFT);
					/* hysterisis in lsb nibble */
					varbuf_append(&b, vstr_temps_hysteresis,
						(cis[i + 2] & SROM11_TEMPS_HYSTERESIS_MASK) >>
						SROM11_TEMPS_HYSTERESIS_SHIFT);
					if (tlen >= 4) {
						varbuf_append(&b, vstr_tempoffset,
						(cis[i + 3]));
						varbuf_append(&b, vstr_tempsense_slope,
						(cis[i + 4]));
						varbuf_append(&b, vstr_temp_corrx,
						(cis[i + 5] & SROM11_TEMPCORRX_MASK) >>
						SROM11_TEMPCORRX_SHIFT);
						varbuf_append(&b, vstr_tempsense_option,
						(cis[i + 5] & SROM11_TEMPSENSE_OPTION_MASK) >>
						SROM11_TEMPSENSE_OPTION_SHIFT);
						varbuf_append(&b, vstr_phycal_tempdelta,
						(cis[i + 6]));
					}
					break;

				case HNBU_FEM_CFG: {
					/* fem_cfg1 */
					uint16 fem_cfg = (cis[i + 2] << 8) + cis[i + 1];
					varbuf_append(&b, vstr_femctrl,
						(fem_cfg & SROM11_FEMCTRL_MASK) >>
						SROM11_FEMCTRL_SHIFT);
					varbuf_append(&b, vstr_papdcap, 2,
						(fem_cfg & SROM11_PAPDCAP_MASK) >>
						SROM11_PAPDCAP_SHIFT);
					varbuf_append(&b, vstr_tworangetssi, 2,
						(fem_cfg & SROM11_TWORANGETSSI_MASK) >>
						SROM11_TWORANGETSSI_SHIFT);
					varbuf_append(&b, vstr_pdgaing, 2,
						(fem_cfg & SROM11_PDGAIN_MASK) >>
						SROM11_PDGAIN_SHIFT);
					varbuf_append(&b, vstr_epagaing, 2,
						(fem_cfg & SROM11_EPAGAIN_MASK) >>
						SROM11_EPAGAIN_SHIFT);
					varbuf_append(&b, vstr_tssiposslopeg, 2,
						(fem_cfg & SROM11_TSSIPOSSLOPE_MASK) >>
						SROM11_TSSIPOSSLOPE_SHIFT);
					/* fem_cfg2 */
					fem_cfg = (cis[i + 4] << 8) + cis[i + 3];
					varbuf_append(&b, vstr_gainctrlsph,
						(fem_cfg & SROM11_GAINCTRLSPH_MASK) >>
						SROM11_GAINCTRLSPH_SHIFT);
					varbuf_append(&b, vstr_papdcap, 5,
						(fem_cfg & SROM11_PAPDCAP_MASK) >>
						SROM11_PAPDCAP_SHIFT);
					varbuf_append(&b, vstr_tworangetssi, 5,
						(fem_cfg & SROM11_TWORANGETSSI_MASK) >>
						SROM11_TWORANGETSSI_SHIFT);
					varbuf_append(&b, vstr_pdgaing, 5,
						(fem_cfg & SROM11_PDGAIN_MASK) >>
						SROM11_PDGAIN_SHIFT);
					varbuf_append(&b, vstr_epagaing, 5,
						(fem_cfg & SROM11_EPAGAIN_MASK) >>
						SROM11_EPAGAIN_SHIFT);
					varbuf_append(&b, vstr_tssiposslopeg, 5,
						(fem_cfg & SROM11_TSSIPOSSLOPE_MASK) >>
						SROM11_TSSIPOSSLOPE_SHIFT);
					break;
				}

				case HNBU_ACPA_C0: {
					const int a = 0;

#ifndef OTP_SKIP_MAXP_PAPARAMS
					varbuf_append(&b, vstr_subband5gver,
					              (cis[i + 2] << 8) + cis[i + 1]);
					/* maxp2g */
					/* Decoupling this touple to program from NVRAM */
					varbuf_append(&b, vstr_maxp2ga, a,
						(cis[i + 4] << 8) + cis[i + 3]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa2g */
					varbuf_append(&b, vstr_pa2ga, a,
						(cis[i + 6] << 8) + cis[i + 5],
						(cis[i + 8] << 8) + cis[i + 7],
						(cis[i + 10] << 8) + cis[i + 9]);
#ifndef OTP_SKIP_MAXP_PAPARAMS
					/* maxp5g */
					varbuf_append(&b, vstr_maxp5ga, a,
						cis[i + 11],
						cis[i + 12],
						cis[i + 13],
						cis[i + 14]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa5g */
					varbuf_append(&b, vstr_pa5ga, a,
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
						(cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23],
						(cis[i + 26] << 8) + cis[i + 25],
						(cis[i + 28] << 8) + cis[i + 27],
						(cis[i + 30] << 8) + cis[i + 29],
						(cis[i + 32] << 8) + cis[i + 31],
						(cis[i + 34] << 8) + cis[i + 33],
						(cis[i + 36] << 8) + cis[i + 35],
						(cis[i + 38] << 8) + cis[i + 37]);
					break;
				}

				case HNBU_ACPA_C1: {
					const int a = 1;

#ifndef OTP_SKIP_MAXP_PAPARAMS
					/* maxp2g */
					/* Decoupling this touple to program from NVRAM */
					varbuf_append(&b, vstr_maxp2ga, a,
						(cis[i + 2] << 8) + cis[i + 1]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa2g */
					varbuf_append(&b, vstr_pa2ga, a,
						(cis[i + 4] << 8) + cis[i + 3],
						(cis[i + 6] << 8) + cis[i + 5],
						(cis[i + 8] << 8) + cis[i + 7]);
#ifndef OTP_SKIP_MAXP_PAPARAMS
					/* maxp5g */
					varbuf_append(&b, vstr_maxp5ga, a,
						cis[i + 9],
						cis[i + 10],
						cis[i + 11],
						cis[i + 12]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa5g */
					varbuf_append(&b, vstr_pa5ga, a,
						(cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
						(cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23],
						(cis[i + 26] << 8) + cis[i + 25],
						(cis[i + 28] << 8) + cis[i + 27],
						(cis[i + 30] << 8) + cis[i + 29],
						(cis[i + 32] << 8) + cis[i + 31],
						(cis[i + 34] << 8) + cis[i + 33],
						(cis[i + 36] << 8) + cis[i + 35]);
					break;
				}

				case HNBU_ACPA_C2: {
					const int a = 2;

#ifndef OTP_SKIP_MAXP_PAPARAMS
					/* maxp2g */
					/* Decoupling this touple to program from NVRAM */
					varbuf_append(&b, vstr_maxp2ga, a,
						(cis[i + 2] << 8) + cis[i + 1]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa2g */
					varbuf_append(&b, vstr_pa2ga, a,
						(cis[i + 4] << 8) + cis[i + 3],
						(cis[i + 6] << 8) + cis[i + 5],
						(cis[i + 8] << 8) + cis[i + 7]);
#ifndef OTP_SKIP_MAXP_PAPARAMS
					/* maxp5g */
					varbuf_append(&b, vstr_maxp5ga, a,
						cis[i + 9],
						cis[i + 10],
						cis[i + 11],
						cis[i + 12]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa5g */
					varbuf_append(&b, vstr_pa5ga, a,
						(cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
						(cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23],
						(cis[i + 26] << 8) + cis[i + 25],
						(cis[i + 28] << 8) + cis[i + 27],
						(cis[i + 30] << 8) + cis[i + 29],
						(cis[i + 32] << 8) + cis[i + 31],
						(cis[i + 34] << 8) + cis[i + 33],
						(cis[i + 36] << 8) + cis[i + 35]);
					break;
				}

				case HNBU_MEAS_PWR:
					varbuf_append(&b, vstr_measpower, cis[i + 1]);
					varbuf_append(&b, vstr_measpowerX, 1, (cis[i + 2]));
					varbuf_append(&b, vstr_measpowerX, 2, (cis[i + 3]));
					varbuf_append(&b, vstr_rawtempsense,
						((cis[i + 5] & 0x1) << 8) + cis[i + 4]);
					break;

				case HNBU_PDOFF:
					varbuf_append(&b, vstr_pdoffsetma, 40, 0,
					      (cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_pdoffsetma, 40, 1,
					      (cis[i + 4] << 8) + cis[i + 3]);
					varbuf_append(&b, vstr_pdoffsetma, 40, 2,
					      (cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_pdoffsetma, 80, 0,
					      (cis[i + 8] << 8) + cis[i + 7]);
					varbuf_append(&b, vstr_pdoffsetma, 80, 1,
					      (cis[i + 10] << 8) + cis[i + 9]);
					varbuf_append(&b, vstr_pdoffsetma, 80, 2,
					      (cis[i + 12] << 8) + cis[i + 11]);
					break;

				case HNBU_ACPPR_2GPO:
					varbuf_append(&b, vstr_dot11agofdmhrbw202gpo,
					              (cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_ofdmlrbw202gpo,
					              (cis[i + 4] << 8) + cis[i + 3]);

					if (tlen < 13) break;
					varbuf_append(&b, vstr_sb20in40dot11agofdm2gpo,
					              (cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_sb20in80dot11agofdm2gpo,
					              (cis[i + 8] << 8) + cis[i + 7]);
					varbuf_append(&b, vstr_sb20in40ofdmlrbw202gpo,
					              (cis[i + 10] << 8) + cis[i + 9]);
					varbuf_append(&b, vstr_sb20in80ofdmlrbw202gpo,
					              (cis[i + 12] << 8) + cis[i + 11]);
					break;

				case HNBU_ACPPR_5GPO:
					varbuf_append(&b, vstr_mcsbw805gpo, 'l',
						(cis[i + 4] << 24) + (cis[i + 3] << 16) +
						(cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_mcsbw1605gpo, 'l',
						(cis[i + 8] << 24) + (cis[i + 7] << 16) +
						(cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_mcsbw805gpo, 'm',
						(cis[i + 12] << 24) + (cis[i + 11] << 16) +
						(cis[i + 10] << 8) + cis[i + 9]);
					varbuf_append(&b, vstr_mcsbw1605gpo, 'm',
						(cis[i + 16] << 24) + (cis[i + 15] << 16) +
						(cis[i + 14] << 8) + cis[i + 13]);
					varbuf_append(&b, vstr_mcsbw805gpo, 'h',
						(cis[i + 20] << 24) + (cis[i + 19] << 16) +
						(cis[i + 18] << 8) + cis[i + 17]);
					varbuf_append(&b, vstr_mcsbw1605gpo, 'h',
						(cis[i + 24] << 24) + (cis[i + 23] << 16) +
						(cis[i + 22] << 8) + cis[i + 21]);
					varbuf_append(&b, vstr_mcslr5gpo, 'l',
					              (cis[i + 26] << 8) + cis[i + 25]);
					varbuf_append(&b, vstr_mcslr5gpo, 'm',
					              (cis[i + 28] << 8) + cis[i + 27]);
					varbuf_append(&b, vstr_mcslr5gpo, 'h',
					              (cis[i + 30] << 8) + cis[i + 29]);

					if (tlen < 51) break;
					varbuf_append(&b, vstr_mcsbw80p805gpo, 'l',
						(cis[i + 34] << 24) + (cis[i + 33] << 16) +
						(cis[i + 32] << 8) + cis[i + 31]);
					varbuf_append(&b, vstr_mcsbw80p805gpo, 'm',
						(cis[i + 38] << 24) + (cis[i + 37] << 16) +
						(cis[i + 36] << 8) + cis[i + 35]);
					varbuf_append(&b, vstr_mcsbw80p805gpo, 'h',
						(cis[i + 42] << 24) + (cis[i + 41] << 16) +
						(cis[i + 40] << 8) + cis[i + 39]);
					varbuf_append(&b, vstr_mcsbw80p805g1po, 'x',
						(cis[i + 46] << 24) + (cis[i + 45] << 16) +
						(cis[i + 44] << 8) + cis[i + 43]);
					varbuf_append(&b, vstr_mcslr5g1po, 'x',
					              (cis[i + 48] << 8) + cis[i + 47]);
					varbuf_append(&b, vstr_mcslr5g80p80po,
					              (cis[i + 50] << 8) + cis[i + 49]);
					varbuf_append(&b, vstr_mcsbw805g1po, 'x',
						(cis[i + 54] << 24) + (cis[i + 53] << 16) +
						(cis[i + 52] << 8) + cis[i + 51]);
					varbuf_append(&b, vstr_mcsbw1605g1po, 'x',
						(cis[i + 58] << 24) + (cis[i + 57] << 16) +
						(cis[i + 56] << 8) + cis[i + 55]);

					break;

				case HNBU_MCS5Gx1PO:
					varbuf_append(&b, vstr_mcsbw205g1po, 'x',
						(cis[i + 4] << 24) + (cis[i + 3] << 16) +
						(cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_mcsbw405g1po, 'x',
						(cis[i + 8] << 24) + (cis[i + 7] << 16) +
						(cis[i + 6] << 8) + cis[i + 5]);
					break;

				case HNBU_ACPPR_SBPO:
					varbuf_append(&b, vstr_sb20in40rpo, 'h',
					              (cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_sb20in80and160r5gpo, 'h', 'l',
					              (cis[i + 4] << 8) + cis[i + 3]);
					varbuf_append(&b, vstr_sb40and80r5gpo, 'h', 'l',
					              (cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_sb20in80and160r5gpo, 'h', 'm',
					              (cis[i + 8] << 8) + cis[i + 7]);
					varbuf_append(&b, vstr_sb40and80r5gpo, 'h', 'm',
					              (cis[i + 10] << 8) + cis[i + 9]);
					varbuf_append(&b, vstr_sb20in80and160r5gpo, 'h', 'h',
					              (cis[i + 12] << 8) + cis[i + 11]);
					varbuf_append(&b, vstr_sb40and80r5gpo, 'h', 'h',
					              (cis[i + 14] << 8) + cis[i + 13]);
					varbuf_append(&b, vstr_sb20in40rpo, 'l',
					              (cis[i + 16] << 8) + cis[i + 15]);
					varbuf_append(&b, vstr_sb20in80and160r5gpo, 'l', 'l',
					              (cis[i + 18] << 8) + cis[i + 17]);
					varbuf_append(&b, vstr_sb40and80r5gpo, 'l', 'l',
					              (cis[i + 20] << 8) + cis[i + 19]);
					varbuf_append(&b, vstr_sb20in80and160r5gpo, 'l', 'm',
					              (cis[i + 22] << 8) + cis[i + 21]);
					varbuf_append(&b, vstr_sb40and80r5gpo, 'l', 'm',
					              (cis[i + 24] << 8) + cis[i + 23]);
					varbuf_append(&b, vstr_sb20in80and160r5gpo, 'l', 'h',
					              (cis[i + 26] << 8) + cis[i + 25]);
					varbuf_append(&b, vstr_sb40and80r5gpo, 'l', 'h',
					              (cis[i + 28] << 8) + cis[i + 27]);
					varbuf_append(&b, vstr_dot11agduprpo, 'h',
						(cis[i + 32] << 24) + (cis[i + 31] << 16) +
						(cis[i + 30] << 8) + cis[i + 29]);
					varbuf_append(&b, vstr_dot11agduprpo, 'l',
						(cis[i + 36] << 24) + (cis[i + 35] << 16) +
						(cis[i + 34] << 8) + cis[i + 33]);

					if (tlen < 49) break;
					varbuf_append(&b, vstr_sb20in40and80rpo, 'h',
						(cis[i + 38] << 8) + cis[i + 37]);
					varbuf_append(&b, vstr_sb20in40and80rpo, 'l',
						(cis[i + 40] << 8) + cis[i + 39]);
					varbuf_append(&b, vstr_sb20in80and160r5g1po, 'h', 'x',
						(cis[i + 42] << 8) + cis[i + 41]);
					varbuf_append(&b, vstr_sb20in80and160r5g1po, 'l', 'x',
						(cis[i + 44] << 8) + cis[i + 43]);
					varbuf_append(&b, vstr_sb40and80r5g1po, 'h', 'x',
						(cis[i + 46] << 8) + cis[i + 45]);
					varbuf_append(&b, vstr_sb40and80r5g1po, 'l', 'x',
						(cis[i + 48] << 8) + cis[i + 47]);
					break;

				case HNBU_ACPPR_SB8080_PO:
					varbuf_append(&b, vstr_sb2040and80in80p80r5gpo, 'h', 'l',
						(cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_sb2040and80in80p80r5gpo, 'l', 'l',
						(cis[i + 4] << 8) + cis[i + 3]);
					varbuf_append(&b, vstr_sb2040and80in80p80r5gpo, 'h', 'm',
						(cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_sb2040and80in80p80r5gpo, 'l', 'm',
						(cis[i + 8] << 8) + cis[i + 7]);
					varbuf_append(&b, vstr_sb2040and80in80p80r5gpo, 'h', 'h',
						(cis[i + 10] << 8) + cis[i + 9]);
					varbuf_append(&b, vstr_sb2040and80in80p80r5gpo, 'l', 'h',
						(cis[i + 12] << 8) + cis[i + 11]);
					varbuf_append(&b, vstr_sb2040and80in80p80r5g1po, 'h', 'x',
						(cis[i + 14] << 8) + cis[i + 13]);
					varbuf_append(&b, vstr_sb2040and80in80p80r5g1po, 'l', 'x',
						(cis[i + 16] << 8) + cis[i + 15]);
					varbuf_append(&b, vstr_sb20in80p80r5gpo, 'h',
						(cis[i + 18] << 8) + cis[i + 17]);
					varbuf_append(&b, vstr_sb20in80p80r5gpo, 'l',
						(cis[i + 20] << 8) + cis[i + 19]);
					varbuf_append(&b, vstr_dot11agduppo,
						(cis[i + 22] << 8) + cis[i + 21]);
					break;

				case HNBU_NOISELVL:
					/* noiselvl2g */
					varbuf_append(&b, vstr_noiselvl2ga, 0,
					              (cis[i + 1] & 0x1f));
					varbuf_append(&b, vstr_noiselvl2ga, 1,
					              (cis[i + 2] & 0x1f));
					varbuf_append(&b, vstr_noiselvl2ga, 2,
					              (cis[i + 3] & 0x1f));
					/* noiselvl5g */
					varbuf_append(&b, vstr_noiselvl5ga, 0,
					              (cis[i + 4] & 0x1f),
					              (cis[i + 5] & 0x1f),
					              (cis[i + 6] & 0x1f),
					              (cis[i + 7] & 0x1f));
					varbuf_append(&b, vstr_noiselvl5ga, 1,
					              (cis[i + 8] & 0x1f),
					              (cis[i + 9] & 0x1f),
					              (cis[i + 10] & 0x1f),
					              (cis[i + 11] & 0x1f));
					varbuf_append(&b, vstr_noiselvl5ga, 2,
					              (cis[i + 12] & 0x1f),
					              (cis[i + 13] & 0x1f),
					              (cis[i + 14] & 0x1f),
					              (cis[i + 15] & 0x1f));
					break;

				case HNBU_RXGAIN_ERR:
					varbuf_append(&b, vstr_rxgainerr2ga, 0,
					              (cis[i + 1] & 0x3f));
					varbuf_append(&b, vstr_rxgainerr2ga, 1,
					              (cis[i + 2] & 0x1f));
					varbuf_append(&b, vstr_rxgainerr2ga, 2,
					              (cis[i + 3] & 0x1f));
					varbuf_append(&b, vstr_rxgainerr5ga, 0,
					              (cis[i + 4] & 0x3f),
					              (cis[i + 5] & 0x3f),
					              (cis[i + 6] & 0x3f),
					              (cis[i + 7] & 0x3f));
					varbuf_append(&b, vstr_rxgainerr5ga, 1,
					              (cis[i + 8] & 0x1f),
					              (cis[i + 9] & 0x1f),
					              (cis[i + 10] & 0x1f),
					              (cis[i + 11] & 0x1f));
					varbuf_append(&b, vstr_rxgainerr5ga, 2,
					              (cis[i + 12] & 0x1f),
					              (cis[i + 13] & 0x1f),
					              (cis[i + 14] & 0x1f),
					              (cis[i + 15] & 0x1f));
					break;

				case HNBU_AGBGA:
					varbuf_append(&b, vstr_agbg, 0, cis[i + 1]);
					varbuf_append(&b, vstr_agbg, 1, cis[i + 2]);
					varbuf_append(&b, vstr_agbg, 2, cis[i + 3]);
					varbuf_append(&b, vstr_aga, 0, cis[i + 4]);
					varbuf_append(&b, vstr_aga, 1, cis[i + 5]);
					varbuf_append(&b, vstr_aga, 2, cis[i + 6]);
					break;

				case HNBU_ACRXGAINS_C0: {
					int a = 0;

					/* rxgains */
					uint16 rxgains = (cis[i + 2] << 8) + cis[i + 1];
					varbuf_append(&b, vstr_rxgainsgtrelnabypa, 5, a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrisoa, 5, a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgelnagaina, 5, a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrelnabypa, 2, a,
						(rxgains & SROM11_RXGAINS2GTRELNABYPA_MASK) >>
						SROM11_RXGAINS2GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrisoa, 2, a,
						(rxgains & SROM11_RXGAINS2GTRISOA_MASK) >>
						SROM11_RXGAINS2GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgelnagaina, 2, a,
						(rxgains & SROM11_RXGAINS2GELNAGAINA_MASK) >>
						SROM11_RXGAINS2GELNAGAINA_SHIFT);
					/* rxgains1 */
					rxgains = (cis[i + 4] << 8) + cis[i + 3];
					varbuf_append(&b, vstr_rxgainsgxtrelnabypa, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrisoa, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxelnagaina, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrelnabypa, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrisoa, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxelnagaina, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					break;
				}

				case HNBU_ACRXGAINS_C1: {
					int a = 1;

					/* rxgains */
					uint16 rxgains = (cis[i + 2] << 8) + cis[i + 1];
					varbuf_append(&b, vstr_rxgainsgtrelnabypa, 5, a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrisoa, 5, a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgelnagaina, 5, a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrelnabypa, 2, a,
						(rxgains & SROM11_RXGAINS2GTRELNABYPA_MASK) >>
						SROM11_RXGAINS2GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrisoa, 2, a,
						(rxgains & SROM11_RXGAINS2GTRISOA_MASK) >>
						SROM11_RXGAINS2GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgelnagaina, 2, a,
						(rxgains & SROM11_RXGAINS2GELNAGAINA_MASK) >>
						SROM11_RXGAINS2GELNAGAINA_SHIFT);
					/* rxgains1 */
					rxgains = (cis[i + 4] << 8) + cis[i + 3];
					varbuf_append(&b, vstr_rxgainsgxtrelnabypa, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrisoa, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxelnagaina, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrelnabypa, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrisoa, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxelnagaina, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					break;
				}

				case HNBU_ACRXGAINS_C2: {
					int a = 2;

					/* rxgains */
					uint16 rxgains = (cis[i + 2] << 8) + cis[i + 1];
					varbuf_append(&b, vstr_rxgainsgtrelnabypa, 5, a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrisoa, 5, a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgelnagaina, 5, a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrelnabypa, 2, a,
						(rxgains & SROM11_RXGAINS2GTRELNABYPA_MASK) >>
						SROM11_RXGAINS2GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgtrisoa, 2, a,
						(rxgains & SROM11_RXGAINS2GTRISOA_MASK) >>
						SROM11_RXGAINS2GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgelnagaina, 2, a,
						(rxgains & SROM11_RXGAINS2GELNAGAINA_MASK) >>
						SROM11_RXGAINS2GELNAGAINA_SHIFT);
					/* rxgains1 */
					rxgains = (cis[i + 4] << 8) + cis[i + 3];
					varbuf_append(&b, vstr_rxgainsgxtrelnabypa, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrisoa, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxelnagaina, 5, 'h', a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrelnabypa, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GTRELNABYPA_MASK) >>
						SROM11_RXGAINS5GTRELNABYPA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxtrisoa, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GTRISOA_MASK) >>
						SROM11_RXGAINS5GTRISOA_SHIFT);
					varbuf_append(&b, vstr_rxgainsgxelnagaina, 5, 'm', a,
						(rxgains & SROM11_RXGAINS5GELNAGAINA_MASK) >>
						SROM11_RXGAINS5GELNAGAINA_SHIFT);
					break;
				}

				case HNBU_TXDUTY: {
					varbuf_append(&b, vstr_txduty_ofdm, 40,
					              (cis[i + 2] << 8) + cis[i + 1]);
					varbuf_append(&b, vstr_txduty_thresh, 40,
					              (cis[i + 4] << 8) + cis[i + 3]);
					varbuf_append(&b, vstr_txduty_ofdm, 80,
					              (cis[i + 6] << 8) + cis[i + 5]);
					varbuf_append(&b, vstr_txduty_thresh, 80,
					              (cis[i + 8] << 8) + cis[i + 7]);
					break;
				}

				case HNBU_UUID: {
					/* uuid format 12345678-1234-5678-1234-567812345678 */

					char uuidstr[37]; /* 32 ids, 4 '-', 1 Null */

					snprintf(uuidstr, sizeof(uuidstr),
						rstr_uuidstr,
						cis[i + 1], cis[i + 2], cis[i + 3], cis[i + 4],
						cis[i + 5], cis[i + 6], cis[i + 7], cis[i + 8],
						cis[i + 9], cis[i + 10], cis[i + 11], cis[i + 12],
						cis[i + 13], cis[i + 14], cis[i + 15], cis[i + 16]);

					varbuf_append(&b, vstr_uuid, uuidstr);
					break;
				}

				case HNBU_WOWLGPIO:
					varbuf_append(&b, vstr_wowlgpio, ((cis[i + 1]) & 0x7F));
					varbuf_append(&b, vstr_wowlgpiopol,
						(((cis[i + 1]) >> 7) & 0x1));
					break;

#endif /* !BCM_BOOTLOADER */
#ifdef BCMUSBDEV_COMPOSITE
				case HNBU_USBDESC_COMPOSITE:
					varbuf_append(&b, vstr_usbdesc_composite,
						(cis[i + 2] << 8) | cis[i + 1]);
					break;
#endif /* BCMUSBDEV_COMPOSITE */
				case HNBU_USBUTMI_CTL:
					varbuf_append(&b, vstr_usbutmi_ctl,
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_USBSSPHY_UTMI_CTL0:
					varbuf_append(&b, vstr_usbssphy_utmi_ctl0,
						(cis[i + 4] << 24) | (cis[i + 3] << 16) |
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_USBSSPHY_UTMI_CTL1:
					varbuf_append(&b, vstr_usbssphy_utmi_ctl1,
						(cis[i + 4] << 24) | (cis[i + 3] << 16) |
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_USBSSPHY_UTMI_CTL2:
					varbuf_append(&b, vstr_usbssphy_utmi_ctl2,
						(cis[i + 4] << 24) | (cis[i + 3] << 16) |
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_USBSSPHY_SLEEP0:
					varbuf_append(&b, vstr_usbssphy_sleep0,
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_USBSSPHY_SLEEP1:
					varbuf_append(&b, vstr_usbssphy_sleep1,
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_USBSSPHY_SLEEP2:
					varbuf_append(&b, vstr_usbssphy_sleep2,
						(cis[i + 2] << 8) | cis[i + 1]);
					break;

				case HNBU_USBSSPHY_SLEEP3:
					varbuf_append(&b, vstr_usbssphy_sleep3,
						(cis[i + 2] << 8) | cis[i + 1]);
					break;
				case HNBU_USBSSPHY_MDIO: {
					uint8 setnum;
					uint16 k;

					setnum = (cis[i + 1])/4;
					if (setnum == 0)
						break;
					for (j = 0; j < setnum; j++) {
						k = j*12;
						varbuf_append(&b, vstr_usbssphy_mdio, j,
						(cis[i+4+k]<<16) | (cis[i+3+k]<<8) | cis[i+2+k],
						(cis[i+7+k]<<16) | (cis[i+6+k]<<8) | cis[i+5+k],
						(cis[i+10+k]<<16) | (cis[i+9+k]<<8) | cis[i+8+k],
						(cis[i+13+k]<<16) | (cis[i+12+k]<<8) | cis[i+11+k]);
					}
					break;
				}
				case HNBU_USB30PHY_NOSS:
					varbuf_append(&b, vstr_usb30phy_noss, cis[i + 1]);
					break;
				case HNBU_USB30PHY_U1U2:
					varbuf_append(&b, vstr_usb30phy_u1u2, cis[i + 1]);
					break;
				case HNBU_USB30PHY_REGS:
					varbuf_append(&b, vstr_usb30phy_regs, 0,
						cis[i+4]|cis[i+3]|cis[i+2]|cis[i+1],
						cis[i+8]|cis[i+7]|cis[i+6]|cis[i+5],
						cis[i+12]|cis[i+11]|cis[i+10]|cis[i+9],
						cis[i+16]|cis[i+15]|cis[i+14]|cis[i+13]);
					varbuf_append(&b, vstr_usb30phy_regs, 1,
						cis[i+20]|cis[i+19]|cis[i+18]|cis[i+17],
						cis[i+24]|cis[i+23]|cis[i+22]|cis[i+21],
						cis[i+28]|cis[i+27]|cis[i+26]|cis[i+25],
						cis[i+32]|cis[i+31]|cis[i+30]|cis[i+29]);

					break;

				case HNBU_PDOFF_2G: {
					uint16 pdoff_2g = (cis[i + 2] << 8) + cis[i + 1];
					varbuf_append(&b, vstr_pdoffset2gma, 40, 0,
						(pdoff_2g & SROM11_PDOFF_2G_40M_A0_MASK) >>
						SROM11_PDOFF_2G_40M_A0_SHIFT);
					varbuf_append(&b, vstr_pdoffset2gma, 40, 1,
						(pdoff_2g & SROM11_PDOFF_2G_40M_A1_MASK) >>
						SROM11_PDOFF_2G_40M_A1_SHIFT);
					varbuf_append(&b, vstr_pdoffset2gma, 40, 2,
						(pdoff_2g & SROM11_PDOFF_2G_40M_A2_MASK) >>
						SROM11_PDOFF_2G_40M_A2_SHIFT);
					varbuf_append(&b, vstr_pdoffset2gmvalid, 40,
						(pdoff_2g & SROM11_PDOFF_2G_40M_VALID_MASK) >>
						SROM11_PDOFF_2G_40M_VALID_SHIFT);
					break;
				}

				case HNBU_ACPA_CCK_C0:
					varbuf_append(&b, vstr_pa2gccka, 0,
					        (cis[i + 2] << 8) + cis[i + 1],
						(cis[i + 4] << 8) + cis[i + 3],
						(cis[i + 6] << 8) + cis[i + 5]);
					break;

				case HNBU_ACPA_CCK_C1:
					varbuf_append(&b, vstr_pa2gccka, 1,
						(cis[i + 2] << 8) + cis[i + 1],
						(cis[i + 4] << 8) + cis[i + 3],
						(cis[i + 6] << 8) + cis[i + 5]);
					break;

				case HNBU_ACPA_40:
					varbuf_append(&b, vstr_pa5gbw40a, 0,
					        (cis[i + 2] << 8) + cis[i + 1],
						(cis[i + 4] << 8) + cis[i + 3],
						(cis[i + 6] << 8) + cis[i + 5],
					        (cis[i + 8] << 8) + cis[i + 7],
						(cis[i + 10] << 8) + cis[i + 9],
						(cis[i + 12] << 8) + cis[i + 11],
					        (cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
					        (cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23]);
					break;

				case HNBU_ACPA_80:
					varbuf_append(&b, vstr_pa5gbw80a, 0,
					        (cis[i + 2] << 8) + cis[i + 1],
						(cis[i + 4] << 8) + cis[i + 3],
						(cis[i + 6] << 8) + cis[i + 5],
					        (cis[i + 8] << 8) + cis[i + 7],
						(cis[i + 10] << 8) + cis[i + 9],
						(cis[i + 12] << 8) + cis[i + 11],
					        (cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
					        (cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23]);
					break;

				case HNBU_ACPA_4080:
					varbuf_append(&b, vstr_pa5gbw4080a, 0,
					        (cis[i + 2] << 8) + cis[i + 1],
						(cis[i + 4] << 8) + cis[i + 3],
						(cis[i + 6] << 8) + cis[i + 5],
					        (cis[i + 8] << 8) + cis[i + 7],
						(cis[i + 10] << 8) + cis[i + 9],
						(cis[i + 12] << 8) + cis[i + 11],
					        (cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
					        (cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23]);
					varbuf_append(&b, vstr_pa5gbw4080a, 1,
					        (cis[i + 26] << 8) + cis[i + 25],
						(cis[i + 28] << 8) + cis[i + 27],
						(cis[i + 30] << 8) + cis[i + 29],
					        (cis[i + 32] << 8) + cis[i + 31],
						(cis[i + 34] << 8) + cis[i + 33],
						(cis[i + 36] << 8) + cis[i + 35],
					        (cis[i + 38] << 8) + cis[i + 37],
						(cis[i + 40] << 8) + cis[i + 39],
						(cis[i + 42] << 8) + cis[i + 41],
					        (cis[i + 44] << 8) + cis[i + 43],
						(cis[i + 46] << 8) + cis[i + 45],
						(cis[i + 48] << 8) + cis[i + 47]);
					break;

				case HNBU_ACPA_4X4C0:
				case HNBU_ACPA_4X4C1:
				case HNBU_ACPA_4X4C2:
				case HNBU_ACPA_4X4C3: {
					int core_num = 0;
					uint8 tuple = cis[i];

					if (tuple == HNBU_ACPA_4X4C1) {
						core_num = 1;
					} else if (tuple == HNBU_ACPA_4X4C2) {
						core_num = 2;
					} else if (tuple == HNBU_ACPA_4X4C3) {
						core_num = 3;
					}

					varbuf_append(&b, vstr_maxp2ga, core_num, cis[i + 1]);
					/* pa2g */
					varbuf_append(&b, vstr_sr13pa2ga, core_num,
						(cis[i + 3] << 8) + cis[i + 2],
						(cis[i + 5] << 8) + cis[i + 4],
						(cis[i + 7] << 8) + cis[i + 6],
						(cis[i + 9] << 8) + cis[i + 8]);
					/* pa2g40 */
					varbuf_append(&b, vstr_pa2g40a, core_num,
						(cis[i + 11] << 8) + cis[i + 10],
						(cis[i + 13] << 8) + cis[i + 12],
						(cis[i + 15] << 8) + cis[i + 14],
						(cis[i + 17] << 8) + cis[i + 16]);
					for (j = 0; j < 5; j++) {
						varbuf_append(&b, vstr_maxp5gba, j, core_num,
							cis[i + j + 18]);
					}
					break;
				}

				case HNBU_ACPA_BW20_4X4C0:
				case HNBU_ACPA_BW40_4X4C0:
				case HNBU_ACPA_BW80_4X4C0:
				case HNBU_ACPA_BW20_4X4C1:
				case HNBU_ACPA_BW40_4X4C1:
				case HNBU_ACPA_BW80_4X4C1:
				case HNBU_ACPA_BW20_4X4C2:
				case HNBU_ACPA_BW40_4X4C2:
				case HNBU_ACPA_BW80_4X4C2:
				case HNBU_ACPA_BW20_4X4C3:
				case HNBU_ACPA_BW40_4X4C3:
				case HNBU_ACPA_BW80_4X4C3: {
					int k = 0;
					char pabuf[140]; /* max: 20 '0x????'s + 19 ','s + 1 Null */
					int core_num = 0, buflen = 0;
					uint8 tuple = cis[i];

					if (tuple == HNBU_ACPA_BW20_4X4C1 ||
						tuple == HNBU_ACPA_BW40_4X4C1 ||
						tuple == HNBU_ACPA_BW80_4X4C1) {
						core_num = 1;
					} else if (tuple == HNBU_ACPA_BW20_4X4C2 ||
						tuple == HNBU_ACPA_BW40_4X4C2 ||
						tuple == HNBU_ACPA_BW80_4X4C2) {
						core_num = 2;
					} else if (tuple == HNBU_ACPA_BW20_4X4C3 ||
						tuple == HNBU_ACPA_BW40_4X4C3 ||
						tuple == HNBU_ACPA_BW80_4X4C3) {
						core_num = 3;
					}

					buflen = sizeof(pabuf);
					for (j = 0; j < 20; j++) { /* cis[i+1] - cis[i+40] */
						k += snprintf(pabuf+k, buflen-k, rstr_hex,
							((cis[i + (2*j) + 2] << 8) +
							cis[i + (2*j) + 1]));
						if (j < 19) {
							k += snprintf(pabuf+k, buflen-k,
								",");
						}
					}

					if (tuple == HNBU_ACPA_BW20_4X4C0 ||
						tuple == HNBU_ACPA_BW20_4X4C1 ||
						tuple == HNBU_ACPA_BW20_4X4C2 ||
						tuple == HNBU_ACPA_BW20_4X4C3) {
						varbuf_append(&b, vstr_sr13pa5ga, core_num, pabuf);
					} else {
						int bw = 40;

						if (tuple == HNBU_ACPA_BW80_4X4C0 ||
							tuple == HNBU_ACPA_BW80_4X4C1 ||
							tuple == HNBU_ACPA_BW80_4X4C2 ||
							tuple == HNBU_ACPA_BW80_4X4C3) {
							bw = 80;
						}
						varbuf_append(&b, vstr_sr13pa5gbwa, bw,
							core_num, pabuf);
					}
					break;
				}

				case HNBU_RSSI_DELTA_2G_B0:
				case HNBU_RSSI_DELTA_2G_B1:
				case HNBU_RSSI_DELTA_2G_B2:
				case HNBU_RSSI_DELTA_2G_B3:
				case HNBU_RSSI_DELTA_2G_B4: {
					uint8 tuple = cis[i];
					uint8 grp;
					if (tuple  == HNBU_RSSI_DELTA_2G_B0) {
						grp = 0;
					} else if (tuple  == HNBU_RSSI_DELTA_2G_B1) {
						grp = 1;
					} else if (tuple  == HNBU_RSSI_DELTA_2G_B2) {
						grp = 2;
					} else if (tuple  == HNBU_RSSI_DELTA_2G_B3) {
						grp = 3;
					} else {
						grp = 4;
					}
					/* 2G Band Gourp = grp */
					varbuf_append(&b, vstr_rssidelta2g, grp,
						cis[i + 1],  cis[i + 2],
						cis[i + 3],  cis[i + 4],
						cis[i + 5],  cis[i + 6],
						cis[i + 7],  cis[i + 8],
						cis[i + 9],  cis[i + 10],
						cis[i + 11], cis[i + 12],
						cis[i + 13], cis[i + 14],
						cis[i + 15], cis[i + 16]);
					break;
				}

				case  HNBU_RSSI_CAL_FREQ_GRP_2G:
					/* 2G Band Gourp Defintion */
					varbuf_append(&b, vstr_rssicalfrqg,
						cis[i + 1],  cis[i + 2],
						cis[i + 3],  cis[i + 4],
						cis[i + 5],  cis[i + 6],
						cis[i + 7]);
					break;

				case HNBU_RSSI_DELTA_5GL:
				case HNBU_RSSI_DELTA_5GML:
				case HNBU_RSSI_DELTA_5GMU:
				case HNBU_RSSI_DELTA_5GH: {
					uint8 tuple = cis[i];
					char *band[] = {"l", "ml", "mu", "h"};
					char *pband;
					if (tuple == HNBU_RSSI_DELTA_5GL) {
						pband = band[0];
					} else if (tuple == HNBU_RSSI_DELTA_5GML) {
						pband = band[1];
					} else if (tuple == HNBU_RSSI_DELTA_5GMU) {
						pband = band[2];
					} else {
						pband = band[3];
					}
					/* 5G Band = band */
					varbuf_append(&b, vstr_rssidelta5g, pband,
						cis[i + 1],  cis[i + 2],
						cis[i + 3],  cis[i + 4],
						cis[i + 5],  cis[i + 6],
						cis[i + 7],  cis[i + 8],
						cis[i + 9],  cis[i + 10],
						cis[i + 11], cis[i + 12],
						cis[i + 13], cis[i + 14],
						cis[i + 15], cis[i + 16],
						cis[i + 17], cis[i + 17],
						cis[i + 19], cis[i + 20],
						cis[i + 21], cis[i + 22],
						cis[i + 9],  cis[i + 24]);
					break;
				}

				case HNBU_ACPA_6G_C0: {
					const int a = 0;
#ifndef OTP_SKIP_MAXP_PAPARAMS
					varbuf_append(&b, vstr_subband6gver,
						(cis[i + 2] << 8) + cis[i + 1]);
					/* maxp5g */
					varbuf_append(&b, vstr_maxp6ga, a,
						cis[i + 3],
						cis[i + 4],
						cis[i + 5],
						cis[i + 6],
						cis[i + 7],
						cis[i + 8]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa5g */
					varbuf_append(&b, vstr_pa6ga, a,
						(cis[i + 10] << 8) + cis[i + 9],
						(cis[i + 12] << 8) + cis[i + 11],
						(cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
						(cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23],
						(cis[i + 26] << 8) + cis[i + 25],
						(cis[i + 28] << 8) + cis[i + 27],
						(cis[i + 30] << 8) + cis[i + 29],
						(cis[i + 32] << 8) + cis[i + 31],
						(cis[i + 34] << 8) + cis[i + 33],
						(cis[i + 36] << 8) + cis[i + 35],
						(cis[i + 38] << 8) + cis[i + 37],
						(cis[i + 40] << 8) + cis[i + 39],
						(cis[i + 42] << 8) + cis[i + 41],
						(cis[i + 44] << 8) + cis[i + 43]);
					break;
				}

				case HNBU_ACPA_6G_C1: {
					const int a = 1;
#ifndef OTP_SKIP_MAXP_PAPARAMS
					/* maxp6g */
					varbuf_append(&b, vstr_maxp6ga, a,
						cis[i + 1],
						cis[i + 2],
						cis[i + 3],
						cis[i + 4],
						cis[i + 5],
						cis[i + 6]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa6g */
					varbuf_append(&b, vstr_pa6ga, a,
						(cis[i + 8] << 8) + cis[i + 7],
						(cis[i + 10] << 8) + cis[i + 9],
						(cis[i + 12] << 8) + cis[i + 11],
						(cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
						(cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23],
						(cis[i + 26] << 8) + cis[i + 25],
						(cis[i + 28] << 8) + cis[i + 27],
						(cis[i + 30] << 8) + cis[i + 29],
						(cis[i + 32] << 8) + cis[i + 31],
						(cis[i + 34] << 8) + cis[i + 33],
						(cis[i + 36] << 8) + cis[i + 35],
						(cis[i + 38] << 8) + cis[i + 37],
						(cis[i + 40] << 8) + cis[i + 39],
						(cis[i + 42] << 8) + cis[i + 41]);
					break;
				}

				case HNBU_ACPA_6G_C2: {
					const int a = 2;
#ifndef OTP_SKIP_MAXP_PAPARAMS
					/* maxp6g */
					varbuf_append(&b, vstr_maxp6ga, a,
						cis[i + 1],
						cis[i + 2],
						cis[i + 3],
						cis[i + 4],
						cis[i + 5],
						cis[i + 6]);
#endif /* OTP_SKIP_MAXP_PAPARAMS */
					/* pa6g */
					varbuf_append(&b, vstr_pa6ga, a,
						(cis[i + 8] << 8) + cis[i + 7],
						(cis[i + 10] << 8) + cis[i + 9],
						(cis[i + 12] << 8) + cis[i + 11],
						(cis[i + 14] << 8) + cis[i + 13],
						(cis[i + 16] << 8) + cis[i + 15],
						(cis[i + 18] << 8) + cis[i + 17],
						(cis[i + 20] << 8) + cis[i + 19],
						(cis[i + 22] << 8) + cis[i + 21],
						(cis[i + 24] << 8) + cis[i + 23],
						(cis[i + 26] << 8) + cis[i + 25],
						(cis[i + 28] << 8) + cis[i + 27],
						(cis[i + 30] << 8) + cis[i + 29],
						(cis[i + 32] << 8) + cis[i + 31],
						(cis[i + 34] << 8) + cis[i + 33],
						(cis[i + 36] << 8) + cis[i + 35],
						(cis[i + 38] << 8) + cis[i + 37],
						(cis[i + 40] << 8) + cis[i + 39],
						(cis[i + 42] << 8) + cis[i + 41]);
					break;
				}

				case HNBU_SUBBAND5GVER:
					varbuf_append(&b, vstr_subband5gver,
					        (cis[i + 2] << 8) + cis[i + 1]);
					break;

				case HNBU_PAPARAMBWVER:
					varbuf_append(&b, vstr_paparambwver, cis[i + 1]);
					break;

				case HNBU_TXBFRPCALS:
				/* note: all 5 rpcal parameters are expected to be */
				/* inside one tuple record, i.e written with one  */
				/* wl wrvar cmd as follows: */
				/* wl wrvar rpcal2g=0x1211 ... rpcal5gb3=0x0  */
					if (tlen != 11 ) { /* sanity check */
						BS_ERROR(("srom_parsecis:incorrect length:%d for"
							" HNBU_TXBFRPCALS tuple\n",
							tlen));
						break;
					}

					varbuf_append(&b, vstr_paparamrpcalvars[0],
						(cis[i + 1] + (cis[i + 2] << 8)));
					varbuf_append(&b, vstr_paparamrpcalvars[1],
						(cis[i + 3]  +  (cis[i + 4] << 8)));
					varbuf_append(&b, vstr_paparamrpcalvars[2],
						(cis[i + 5]  +  (cis[i + 6] << 8)));
					varbuf_append(&b, vstr_paparamrpcalvars[3],
						(cis[i + 7]  +  (cis[i + 8] << 8)));
					varbuf_append(&b, vstr_paparamrpcalvars[4],
						(cis[i + 9]  +  (cis[i + 10] << 8)));
					break;

				case HNBU_GPIO_PULL_DOWN:
					varbuf_append(&b, vstr_gpdn,
					              (cis[i + 4] << 24) |
					              (cis[i + 3] << 16) |
					              (cis[i + 2] << 8) |
					              cis[i + 1]);
					break;

				case HNBU_MACADDR2:
					if (!(ETHER_ISNULLADDR(&cis[i+1])) &&
					    !(ETHER_ISMULTI(&cis[i+1]))) {
						bcm_ether_ntoa((struct ether_addr *)&cis[i + 1],
						               eabuf2);
					}
					break;
				} /* CISTPL_BRCM_HNBU */
				break;
			} /* switch (tup) */

			i += tlen;
		} while (tup != CISTPL_END);
	}

	if (boardnum != -1) {
		varbuf_append(&b, vstr_boardnum, boardnum);
	}

	if (eabuf[0]) {
		varbuf_append(&b, vstr_macaddr, eabuf);
	}

	if (eabuf2[0]) {
		varbuf_append(&b, vstr_macaddr2, eabuf2);
	}

#ifndef BCM_BOOTLOADER
	/* if there is no antenna gain field, set default */
	sromrev = (sromrev == 1u) ? (uint8)getintvar(NULL, rstr_sromrev) : sromrev;
	if (sromrev <= 10u && getvar(NULL, rstr_ag0) == NULL && ag_init == FALSE) {
		varbuf_append(&b, vstr_ag, 0, 0xff);
	}
#endif

	/* final nullbyte terminator */
	ASSERT(b.size >= 1u);
	*b.buf++ = '\0';

	ASSERT((uint)(b.buf - base) <= var_cis_size);

	/* initvars_table() MALLOCs, copies and assigns the MALLOCed buffer to '*vars' */
	err = initvars_table(osh, base /* start */, b.buf /* end */, vars, count);

	MFREE(osh, base, var_cis_size);
	return err;
}
#endif /* !defined(BCMDONGLEHOST) */

/**
 * In chips with chipcommon rev 32 and later, the srom is in chipcommon,
 * not in the bus cores.
 */
static uint16
srom_cc_cmd(si_t *sih, osl_t *osh, volatile void *ccregs, uint32 cmd, uint wordoff, uint16 data)
{
	chipcregs_t *cc = ccregs;
	uint wait_cnt = 1000;
	uint32 byteoff = 0, sprom_size = 0;

	BCM_REFERENCE(sih);
	byteoff = wordoff * 2;

	sprom_size = R_REG(osh, &cc->sromcontrol);
	sprom_size = (sprom_size & SROM_SIZE_MASK) >> SROM_SIZE_SHFT_MASK;
	if (sprom_size == SROM_SIZE_2K)
		sprom_size = 2048;
	else if (sprom_size == SROM_SIZE_512)
		sprom_size = 512;
	else if (sprom_size == SROM_SIZE_128)
		sprom_size = 128;
	if (byteoff >= sprom_size)
		return 0xffff;

	if ((cmd == SRC_OP_READ) || (cmd == SRC_OP_WRITE)) {
		if (sih->ccrev >= 59)
			W_REG(osh, &cc->chipcontrol, (byteoff & SROM16K_BANK_SEL_MASK) >>
				SROM16K_BANK_SHFT_MASK);
		W_REG(osh, &cc->sromaddress, (byteoff  & SROM16K_ADDR_SEL_MASK));
		if (cmd == SRC_OP_WRITE)
			W_REG(osh, &cc->sromdata, data);
	}

	W_REG(osh, &cc->sromcontrol, SRC_START | cmd);

	while (wait_cnt--) {
		if ((R_REG(osh, &cc->sromcontrol) & SRC_BUSY) == 0)
			break;
	}

	if (!wait_cnt) {
		BS_ERROR(("srom_cc_cmd: Command 0x%x timed out\n", cmd));
		return 0xffff;
	}
	if (cmd == SRC_OP_READ)
		return (uint16)R_REG(osh, &cc->sromdata);
	else
		return 0xffff;
}

#define CC_SROM_SHADOW_WSIZE	512	/* 0x800 - 0xC00 */

/**
 * Read in and validate sprom.
 * Return 0 on success, nonzero on error.
 * Returns success on an SPROM containing only ones, unclear if this is intended.
 */
static int
sprom_read_pci(osl_t *osh, si_t *sih, volatile uint16 *sprom, uint wordoff,
	uint16 *buf, uint nwords, bool check_crc)
{
	int err = 0;
	uint i;
	volatile void *ccregs = NULL;
	chipcregs_t *cc = NULL;
	uint32 ccval = 0, sprom_size = 0;
	uint32 sprom_num_words;

	if (BCM43602_CHIP(sih->chip) ||
	    (CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM4352_CHIP_ID)) {
		/* save current control setting */
		ccval = si_chipcontrl_read(sih);
	}

	if (BCM43602_CHIP(sih->chip) ||
		(((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
		(CHIPID(sih->chip) == BCM4352_CHIP_ID)) &&
		(CHIPREV(sih->chiprev) <= 2))) {
		si_chipcontrl_srom4360(sih, TRUE);
	}

	if (FALSE) {
		si_srom_clk_set(sih); /* corrects srom clock frequency */
	}

	ccregs = ((volatile uint8 *)sprom - CC_SROM_OTP);
	cc = ccregs;
	sprom_size = R_REG(osh, &cc->sromcontrol);
	sprom_size = (sprom_size & SROM_SIZE_MASK) >> SROM_SIZE_SHFT_MASK;
	if (sprom_size == SROM_SIZE_2K)
		sprom_size = 2048;
	else if (sprom_size == SROM_SIZE_512)
		sprom_size = 512;
	else if (sprom_size == SROM_SIZE_128)
		sprom_size = 128;
	sprom_num_words = sprom_size/2;

	/* read the sprom */
	for (i = 0; i < nwords; i++) {
		if (sih->ccrev > 31 && ISSIM_ENAB(sih)) {
			/* use indirect since direct is too slow on QT */
			if ((sih->cccaps & CC_CAP_SROM) == 0) {
				err = 1;
				goto error;
			}

			/* hack to get ccregs */
			ccregs = (volatile void *)((volatile uint8 *)sprom - CC_SROM_OTP);
			buf[i] = srom_cc_cmd(sih, osh, ccregs, SRC_OP_READ, wordoff + i, 0);

		} else {
			/* Because of the slow emulation we need to read twice in QT */
			if (ISSIM_ENAB(sih)) {
				buf[i] = R_REG(osh, &sprom[wordoff + i]);
			}

			if ((wordoff + i) >= sprom_num_words) {
				buf[i] = 0xffff;
			} else if ((wordoff + i) >= CC_SROM_SHADOW_WSIZE) {
				/* Srom shadow region in chipcommon is only 512 words
				 * use indirect access for Srom beyond 512 words
				 */
				buf[i] = srom_cc_cmd(sih, osh, ccregs, SRC_OP_READ, wordoff + i, 0);
			} else {
				buf[i] = R_REG(osh, &sprom[wordoff + i]);
			}
		}
		if (i == SROM13_SIGN) {
			if ((buf[SROM13_SIGN] !=  SROM13_SIGNATURE) && (nwords == SROM13_WORDS)) {
				err = 1;
				goto error;
			}
		}
	}

	/* bypass crc checking for simulation to allow srom hack */
	if (ISSIM_ENAB(sih)) {
		goto error;
	}

	if (check_crc) {

		if (buf[0] == 0xffff) {
			/* The hardware thinks that an srom that starts with 0xffff
			 * is blank, regardless of the rest of the content, so declare
			 * it bad.
			 */
			BS_ERROR(("sprom_read_pci: buf[0] = 0x%x, returning bad-crc\n", buf[0]));
			err = 1;
			goto error;
		}

		/* fixup the endianness so crc8 will pass */
		htol16_buf(buf, nwords * 2);
		if (hndcrc8((uint8 *)buf, nwords * 2, CRC8_INIT_VALUE) != CRC8_GOOD_VALUE) {
			/* DBG only pci always read srom4 first, then srom8/9 */
			/* BS_ERROR(("sprom_read_pci: bad crc\n")); */
			err = 1;
		}
		/* now correct the endianness of the byte array */
		ltoh16_buf(buf, nwords * 2);
	}

error:
	if ((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
	    (CHIPID(sih->chip) == BCM4352_CHIP_ID) ||
	    BCM43602_CHIP(sih->chip)) {
		/* Restore config after reading SROM */
		si_chipcontrl_restore(sih, ccval);
	}

	return err;
}

#if !defined(BCMDONGLEHOST)
#if defined(BCMNVRAMW) || defined(BCMNVRAMR)
static int
BCMSROMATTACHFN(otp_read_pci)(osl_t *osh, si_t *sih, uint16 *buf, uint bufsz)
{
	uint8 *otp;
	uint sz = OTP_SZ_MAX/2; /* size in words */
	int err = 0;

	if (bufsz > OTP_SZ_MAX) {
		return BCME_ERROR;
	}

	/* freed in same function */
	if ((otp = MALLOC_NOPERSIST(osh, OTP_SZ_MAX)) == NULL) {
		return BCME_ERROR;
	}

	bzero(otp, OTP_SZ_MAX);

	err = otp_read_region(sih, OTP_HW_RGN, (uint16 *)otp, &sz);

	if (err) {
		MFREE(osh, otp, OTP_SZ_MAX);
		return err;
	}

	bcopy(otp, buf, bufsz);

	/* Check CRC */
	if (((uint16 *)otp)[0] == 0xffff) {
		/* The hardware thinks that an srom that starts with 0xffff
		 * is blank, regardless of the rest of the content, so declare
		 * it bad.
		 */
		BS_ERROR(("otp_read_pci: otp[0] = 0x%x, returning bad-crc\n",
		          ((uint16 *)otp)[0]));
		MFREE(osh, otp, OTP_SZ_MAX);
		return 1;
	}

	/* fixup the endianness so crc8 will pass */
	htol16_buf(otp, OTP_SZ_MAX);
	if (hndcrc8(otp, SROM4_WORDS * 2, CRC8_INIT_VALUE) != CRC8_GOOD_VALUE &&
		hndcrc8(otp, SROM10_WORDS * 2, CRC8_INIT_VALUE) != CRC8_GOOD_VALUE &&
		hndcrc8(otp, SROM11_WORDS * 2, CRC8_INIT_VALUE) != CRC8_GOOD_VALUE &&
		hndcrc8(otp, SROM12_WORDS * 2, CRC8_INIT_VALUE) != CRC8_GOOD_VALUE &&
		hndcrc8(otp, SROM13_WORDS * 2, CRC8_INIT_VALUE) != CRC8_GOOD_VALUE) {
		BS_ERROR(("otp_read_pci: bad crc\n"));
		err = 1;
	}

	MFREE(osh, otp, OTP_SZ_MAX);

	return err;
}
#endif /* defined(BCMNVRAMW) || defined(BCMNVRAMR) */
#endif /* !defined(BCMDONGLEHOST) */

int
srom_otp_write_region_crc(si_t *sih, uint nbytes, uint16* buf16, bool write)
{
#if defined(WLTEST) || defined(BCMDBG)
	int err = 0, crc = 0;
#if !defined(BCMDONGLEHOST)
	uint8 *buf8;

	/* Check nbytes is not odd or too big */
	if ((nbytes & 1) || (nbytes > SROM_MAX))
		return 1;

	/* block invalid buffer size */
	if (nbytes < SROM4_WORDS * 2)
		return BCME_BUFTOOSHORT;
	else if (nbytes > SROM13_WORDS * 2)
		return BCME_BUFTOOLONG;

	/* Verify signatures */
	if (!((buf16[SROM4_SIGN] == SROM4_SIGNATURE) ||
		(buf16[SROM8_SIGN] == SROM4_SIGNATURE) ||
		(buf16[SROM10_SIGN] == SROM4_SIGNATURE) ||
		(buf16[SROM11_SIGN] == SROM11_SIGNATURE)||
		(buf16[SROM12_SIGN] == SROM12_SIGNATURE)||
		(buf16[SROM13_SIGN] == SROM13_SIGNATURE))) {
		BS_ERROR(("srom_otp_write_region_crc: wrong signature SROM4_SIGN %x SROM8_SIGN %x"
			" SROM10_SIGN %x\n",
			buf16[SROM4_SIGN], buf16[SROM8_SIGN], buf16[SROM10_SIGN]));
		return BCME_ERROR;
	}

	/* Check CRC */
	if (buf16[0] == 0xffff) {
		/* The hardware thinks that an srom that starts with 0xffff
		 * is blank, regardless of the rest of the content, so declare
		 * it bad.
		 */
		BS_ERROR(("srom_otp_write_region_crc: invalid buf16[0] = 0x%x\n", buf16[0]));
		goto out;
	}

	buf8 = (uint8*)buf16;
	/* fixup the endianness and then calculate crc */
	htol16_buf(buf8, nbytes);
	crc = ~hndcrc8(buf8, nbytes - 1, CRC8_INIT_VALUE);
	/* now correct the endianness of the byte array */
	ltoh16_buf(buf8, nbytes);

	if (nbytes == SROM11_WORDS * 2)
		buf16[SROM11_CRCREV] = (crc << 8) | (buf16[SROM11_CRCREV] & 0xff);
	else if (nbytes == SROM12_WORDS * 2)
		buf16[SROM12_CRCREV] = (crc << 8) | (buf16[SROM12_CRCREV] & 0xff);
	else if (nbytes == SROM13_WORDS * 2)
		buf16[SROM13_CRCREV] = (crc << 8) | (buf16[SROM13_CRCREV] & 0xff);
	else if (nbytes == SROM10_WORDS * 2)
		buf16[SROM10_CRCREV] = (crc << 8) | (buf16[SROM10_CRCREV] & 0xff);
	else
		buf16[SROM4_CRCREV] = (crc << 8) | (buf16[SROM4_CRCREV] & 0xff);

#ifdef BCMNVRAMW
	/* Write the CRC back */
	if (write)
		err = otp_write_region(sih, OTP_HW_RGN, buf16, nbytes/2, 0);
#endif /* BCMNVRAMW */

out:
#endif /* !defined(BCMDONGLEHOST) */
	return write ? err : crc;
#else
	BCM_REFERENCE(sih);
	BCM_REFERENCE(nbytes);
	BCM_REFERENCE(buf16);
	BCM_REFERENCE(write);
	return 0;
#endif /* WLTEST || BCMDBG */

}

#if !defined(BCMDONGLEHOST)
int
BCMATTACHFN(dbushost_initvars_flash)(si_t *sih, osl_t *osh, char **base, uint len)
{
	return initvars_flash(sih, osh, base, len);
}

/**
 * Find variables with <devpath> from flash. 'base' points to the beginning
 * of the table upon enter and to the end of the table upon exit when success.
 * Return 0 on success, nonzero on error.
 */
static int
BCMATTACHFN(initvars_flash)(si_t *sih, osl_t *osh, char **base, uint len)
{
	char *vp = *base;
	char *flash;
	int err;
	char *s;
	uint l, dl, copy_len;
	char devpath[SI_DEVPATH_BUFSZ], devpath_pcie[SI_DEVPATH_BUFSZ];
	char coded_name[SI_DEVPATH_BUFSZ] = {0};
	int path_len, coded_len, devid_len, pcie_path_len;

	/* allocate memory and read in flash */
	/* freed in same function */
	if (!(flash = MALLOC_NOPERSIST(osh, MAX_NVRAM_SPACE)))
		return BCME_NOMEM;
	if ((err = nvram_getall(flash, MAX_NVRAM_SPACE)))
		goto exit;

	/* create legacy devpath prefix */
	si_devpath(sih, devpath, sizeof(devpath));
	path_len = strlen(devpath);

	if (BUSTYPE(sih->bustype) == PCI_BUS) {
		si_devpath_pcie(sih, devpath_pcie, sizeof(devpath_pcie));
		pcie_path_len = strlen(devpath_pcie);
	} else
		pcie_path_len = 0;

	/* create coded devpath prefix */
	si_coded_devpathvar(sih, coded_name, sizeof(coded_name), "devid");

	/* coded_name now is 'xx:devid, eat ending 'devid' */
	/* to be 'xx:' */
	devid_len = strlen("devid");
	coded_len = strlen(coded_name);
	if (coded_len > devid_len) {
		coded_name[coded_len - devid_len] = '\0';
		coded_len -= devid_len;
	}
	else
		coded_len = 0;

	/* grab vars with the <devpath> prefix or <coded_name> previx in name */
	for (s = flash; s && *s; s += l + 1) {
		l = strlen(s);

		/* skip non-matching variable */
		if (strncmp(s, devpath, path_len) == 0)
			dl = path_len;
		else if (pcie_path_len && strncmp(s, devpath_pcie, pcie_path_len) == 0)
			dl = pcie_path_len;
		else if (coded_len && strncmp(s, coded_name, coded_len) == 0)
			dl = coded_len;
		else
			continue;

		/* is there enough room to copy? */
		copy_len = l - dl + 1;
		if (len < copy_len) {
			err = BCME_BUFTOOSHORT;
			goto exit;
		}

		/* no prefix, just the name=value */
		strlcpy(vp, &s[dl], copy_len);
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

exit:
	MFREE(osh, flash, MAX_NVRAM_SPACE);
	return err;
}
#endif /* !defined(BCMDONGLEHOST) */

#if !defined(BCMUSBDEV_ENABLED) && !defined(BCMSDIODEV_ENABLED) &&	\
	!defined(BCMPCIEDEV_ENABLED)
#if !defined(BCMDONGLEHOST)
/**
 * Initialize nonvolatile variable table from flash.
 * Return 0 on success, nonzero on error.
 */
/* no needs to load the nvram variables from the flash for dongles.
 * These variables are mainly for supporting SROM-less devices although
 * we can use the same machenism to support configuration of multiple
 * cores of the same type.
 */
static int
BCMATTACHFN(initvars_flash_si)(si_t *sih, char **vars, uint *count)
{
	osl_t *osh = si_osh(sih);
	char *vp, *base;
	int err;

	ASSERT(vars != NULL);
	ASSERT(count != NULL);

	/* freed in same function */
	base = vp = MALLOC_NOPERSIST(osh, MAXSZ_NVRAM_VARS);
	ASSERT(vp != NULL);
	if (!vp)
		return BCME_NOMEM;

	if ((err = initvars_flash(sih, osh, &vp, MAXSZ_NVRAM_VARS)) == 0)
		err = initvars_table(osh, base, vp, vars, count);

	MFREE(osh, base, MAXSZ_NVRAM_VARS);

	return err;
}
#endif /* !defined(BCMDONGLEHOST) */
#endif	/* !BCMUSBDEV && !BCMSDIODEV */

#if !defined(BCMDONGLEHOST)

/** returns position of rightmost bit that was set in caller supplied mask */
static uint
mask_shift(uint16 mask)
{
	uint i;
	for (i = 0; i < (sizeof(mask) << 3); i ++) {
		if (mask & (1 << i))
			return i;
	}
	ASSERT(mask);
	return 0;
}

static uint
mask_width(uint16 mask)
{
	int i;
	for (i = (sizeof(mask) << 3) - 1; i >= 0; i --) {
		if (mask & (1 << i))
			return (uint)(i - mask_shift(mask) + 1);
	}
	ASSERT(mask);
	return 0;
}

#ifdef BCMASSERT_SUPPORT
static bool
mask_valid(uint16 mask)
{
	uint shift = mask_shift(mask);
	uint width = mask_width(mask);
	return mask == ((~0 << shift) & ~(~0 << (shift + width)));
}
#endif
#ifdef NVSRCX
void
srom_set_sromvars(char *vars)
{
	if (sromh)
		sromh->_srom_vars = vars;
}
char *
srom_get_sromvars()
{
	if (sromh)
		return sromh->_srom_vars;
	else
		return NULL;
}

srom_info_t *
srom_info_init(osl_t *osh)
{
	sromh = (srom_info_t *) MALLOC_NOPERSIST(osh, sizeof(srom_info_t));
	if (!sromh)
		return NULL;
	sromh->_srom_vars = NULL;
	sromh->is_caldata_prsnt = FALSE;
	return sromh;
}
#endif /* NVSRCX */
/**
 * Parses caller supplied SROM contents into name=value pairs. Global array pci_sromvars[] contains
 * the link between a word offset in SROM and the corresponding NVRAM variable name.'srom' points to
 * the SROM word array. 'off' specifies the offset of the first word 'srom' points to, which should
 * be either 0 or SROM3_SWRG_OFF (full SROM or software region).
 */
static void
BCMATTACHFN(_initvars_srom_pci)(uint8 sromrev, uint16 *srom, uint off, varbuf_t *b)
{
	uint16 w;
	uint32 val;
	const sromvar_t *srv;
	uint width;
	uint flags;
	uint32 sr = (1 << sromrev);
	bool in_array = FALSE;
	static char array_temp[256];
	uint array_curr = 0;
	const char* array_name = NULL;

	varbuf_append(b, "sromrev=%d", sromrev);
#if !defined(SROM15_MEMOPT) && !defined(SROM17_MEMOPT)
	if (sromrev == 15) {
		srv = pci_srom15vars;
	} else if (sromrev == 16) {
		srv = pci_srom16vars;
	} else if (sromrev == 17) {
		srv = pci_srom17vars;
	} else if (sromrev == 18) {
		srv = pci_srom18vars;
	} else {
		srv = pci_sromvars;
	}
#else
#if defined(SROM15_MEMOPT)
	srv = pci_srom15vars;
#endif /* defined(SROM15_MEMOPT) */
#if defined(SROM17_MEMOPT)
	srv = pci_srom17vars;
#endif /* defined(SROM17_MEMOPT) */
#endif /* !defined(SROM15_MEMOPT) && !defined(SROM17_MEMOPT) */

	for (; srv->name != NULL; srv ++) {
		const char *name;
		static bool in_array2 = FALSE;
		static char array_temp2[256];
		static uint array_curr2 = 0;
		static const char* array_name2 = NULL;

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
			char eabuf[ETHER_ADDR_STR_LEN];
			struct ether_addr ea;

			ea.octet[0] = (srom[srv->off - off] >> 8) & 0xff;
			ea.octet[1] = srom[srv->off - off] & 0xff;
			ea.octet[2] = (srom[srv->off + 1 - off] >> 8) & 0xff;
			ea.octet[3] = srom[srv->off + 1 - off] & 0xff;
			ea.octet[4] = (srom[srv->off + 2 - off] >> 8) & 0xff;
			ea.octet[5] = srom[srv->off + 2 - off] & 0xff;
			bcm_ether_ntoa(&ea, eabuf);

			varbuf_append(b, "%s=%s", name, eabuf);
		} else {
			ASSERT(mask_valid(srv->mask));
			ASSERT(mask_width(srv->mask));

			/* Start of an array */
			if (sromrev >= 10 && (srv->flags & SRFL_ARRAY) && !in_array2) {
				array_curr2 = 0;
				array_name2 = (const char*)srv->name;
				bzero((void*)array_temp2, sizeof(array_temp2));
				in_array2 = TRUE;
			}

			w = srom[srv->off - off];
			val = (w & srv->mask) >> mask_shift(srv->mask);
			width = mask_width(srv->mask);

			while (srv->flags & SRFL_MORE) {
				srv ++;
				ASSERT(srv->name != NULL);

				if (srv->off == 0 || srv->off < off)
					continue;

				ASSERT(mask_valid(srv->mask));
				ASSERT(mask_width(srv->mask));

				w = srom[srv->off - off];
				val += ((w & srv->mask) >> mask_shift(srv->mask)) << width;
				width += mask_width(srv->mask);
			}

			if ((flags & SRFL_NOFFS) && ((int)val == (1 << width) - 1))
				continue;

			/* Array support starts in sromrev 10. Skip arrays for sromrev <= 9 */
			if (sromrev <= 9 && srv->flags & SRFL_ARRAY) {
				while (srv->flags & SRFL_ARRAY)
					srv ++;
				srv ++;
			}

			if (in_array2) {
				int ret;

				if (flags & SRFL_PRHEX) {
					ret = snprintf(array_temp2 + array_curr2,
						sizeof(array_temp2) - array_curr2, "0x%x,", val);
				} else if ((flags & SRFL_PRSIGN) &&
					(val & (1 << (width - 1)))) {
					ret = snprintf(array_temp2 + array_curr2,
						sizeof(array_temp2) - array_curr2, "%d,",
						(int)(val | (~0 << width)));
				} else {
					ret = snprintf(array_temp2 + array_curr2,
						sizeof(array_temp2) - array_curr2, "%u,", val);
				}

				if (ret > 0) {
					array_curr2 += ret;
				} else {
					BS_ERROR(("_initvars_srom_pci: array %s parsing error."
						" buffer too short.\n",
						array_name2));
					ASSERT(0);

					/* buffer too small, skip this param */
					while (srv->flags & SRFL_ARRAY)
						srv ++;
					srv ++;
					in_array2 = FALSE;
					continue;
				}

				if (!(srv->flags & SRFL_ARRAY)) { /* Array ends */
					/* Remove the last ',' */
					array_temp2[array_curr2-1] = '\0';
					in_array2 = FALSE;
					varbuf_append(b, "%s=%s", array_name2, array_temp2);
				}
			} else if (flags & SRFL_CCODE) {
				if (val == 0)
					varbuf_append(b, "ccode=");
				else
					varbuf_append(b, "ccode=%c%c", (val >> 8), (val & 0xff));
			} else if (flags & SRFL_PRHEX) {
				varbuf_append(b, "%s=0x%x", name, val);
			} else if ((flags & SRFL_PRSIGN) && (val & (1 << (width - 1)))) {
				varbuf_append(b, "%s=%d", name, (int)(val | (~0 << width)));
			} else {
				varbuf_append(b, "%s=%u", name, val);
			}
		}
	}

	if ((sromrev >= 4) && (sromrev != 16) && (sromrev != 18)) {
		/* Do per-path variables */
		uint p, pb, psz, path_num;

		if  ((sromrev == 17) || (sromrev == 15)) {
			pb = psz = 0;
			path_num = 0;
			if (sromh)
				sromh->is_caldata_prsnt = TRUE;
		} else if  (sromrev >= 13) {
			pb = SROM13_PATH0;
			psz = SROM13_PATH1 - SROM13_PATH0;
			path_num = MAX_PATH_SROM_13;
		} else if  (sromrev >= 12) {
			pb = SROM12_PATH0;
			psz = SROM12_PATH1 - SROM12_PATH0;
			path_num = MAX_PATH_SROM_12;
		} else if (sromrev >= 11) {
			pb = SROM11_PATH0;
			psz = SROM11_PATH1 - SROM11_PATH0;
			path_num = MAX_PATH_SROM_11;
		} else if (sromrev >= 8) {
			pb = SROM8_PATH0;
			psz = SROM8_PATH1 - SROM8_PATH0;
			path_num = MAX_PATH_SROM;
		} else {
			pb = SROM4_PATH0;
			psz = SROM4_PATH1 - SROM4_PATH0;
			path_num = MAX_PATH_SROM;
		}

		for (p = 0; p < path_num; p++) {
			for (srv = perpath_pci_sromvars; srv->name != NULL; srv ++) {

				if ((srv->revmask & sr) == 0)
					continue;

				if (pb + srv->off < off)
					continue;

				/* This entry is for mfgc only. Don't generate param for it, */
				if (srv->flags & SRFL_NOVAR)
					continue;

				/* Start of an array */
				if (sromrev >= 10 && (srv->flags & SRFL_ARRAY) && !in_array) {
					array_curr = 0;
					array_name = (const char*)srv->name;
					bzero((void*)array_temp, sizeof(array_temp));
					in_array = TRUE;
				}

				w = srom[pb + srv->off - off];

				ASSERT(mask_valid(srv->mask));
				val = (w & srv->mask) >> mask_shift(srv->mask);
				width = mask_width(srv->mask);

				flags = srv->flags;

				/* Cheating: no per-path var is more than 1 word */

				if ((srv->flags & SRFL_NOFFS) && ((int)val == (1 << width) - 1))
					continue;

				if (in_array) {
					int ret;

					if (flags & SRFL_PRHEX) {
						ret = snprintf(array_temp + array_curr,
						  sizeof(array_temp) - array_curr, "0x%x,", val);
					} else if ((flags & SRFL_PRSIGN) &&
						(val & (1 << (width - 1)))) {
						ret = snprintf(array_temp + array_curr,
							sizeof(array_temp) - array_curr, "%d,",
							(int)(val | (~0 << width)));
					} else {
						ret = snprintf(array_temp + array_curr,
						  sizeof(array_temp) - array_curr, "%u,", val);
					}

					if (ret > 0) {
						array_curr += ret;
					} else {
						BS_ERROR(
						("_initvars_srom_pci: array %s parsing error."
						" buffer too short.\n",
						array_name));
						ASSERT(0);

						/* buffer too small, skip this param */
						while (srv->flags & SRFL_ARRAY)
							srv ++;
						srv ++;
						in_array = FALSE;
						continue;
					}

					if (!(srv->flags & SRFL_ARRAY)) { /* Array ends */
						/* Remove the last ',' */
						array_temp[array_curr-1] = '\0';
						in_array = FALSE;
						varbuf_append(b, "%s%d=%s",
							array_name, p, array_temp);
					}
				} else if (srv->flags & SRFL_PRHEX)
					varbuf_append(b, "%s%d=0x%x", srv->name, p, val);
				else
					varbuf_append(b, "%s%d=%d", srv->name, p, val);
			}
			if (sromrev >= 13 && (p == (MAX_PATH_SROM_13 - 2))) {
				psz = SROM13_PATH3 - SROM13_PATH2;
			}
			pb += psz;
		}
	} /* per path variables */
} /* _initvars_srom_pci */

int
BCMATTACHFN(get_srom_pci_caldata_size)(uint32 sromrev)
{
	uint32 caldata_size;

	switch (sromrev) {
		case 15:
			caldata_size = (SROM15_CALDATA_WORDS * 2);
			break;
		case 17:
			caldata_size = (SROM17_CALDATA_WORDS * 2);
			break;
		default:
			caldata_size = 0;
			break;
	}
	return caldata_size;
}

uint32
BCMATTACHFN(get_srom_size)(uint32 sromrev)
{
	uint32 size;

	switch (sromrev) {
		case 15:
			size = (SROM15_WORDS * 2);
			break;
		case 17:
			size = (SROM17_WORDS * 2);
			break;
		default:
			size = 0;
			break;
	}
	return size;
}
#if defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL)

int
BCMATTACHFN(_initvars_srom_pci_caldata)(si_t *sih, uint16 *srom, uint32 sromrev)
{
	int err = BCME_ERROR;

	if (sromh && (!sromh->is_caldata_prsnt)) {
		return err;
	}

	if (si_is_sprom_available(sih)) {
		uint32 caldata_size;

		caldata_size = get_srom_pci_caldata_size(sromrev);
		memcpy(srom, caldata_array, caldata_size);
		err = BCME_OK;
	}
	return err;
}
#endif /* defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL) */
/**
 * Initialize nonvolatile variable table from sprom, or OTP when SPROM is not available, or
 * optionally a set of 'defaultsromvars' (compiled-in) variables when both OTP and SPROM bear no
 * contents.
 *
 * On success, a buffer containing var/val pairs is allocated and returned in params vars and count.
 *
 * Return 0 on success, nonzero on error.
 */
static int
BCMATTACHFN(initvars_srom_pci)(si_t *sih, volatile void *curmap, char **vars, uint *count)
{
	uint16 *srom;
	volatile uint16 *sromwindow;
	uint8 sromrev = 0;
	uint32 sr;
	varbuf_t b;
	char *vp, *base = NULL;
	osl_t *osh = si_osh(sih);
	bool flash = FALSE;
	int err = 0;
#if defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL)
	uint16 cal_wordoffset;
#endif

	/*
	 * Apply CRC over SROM content regardless SROM is present or not, and use variable
	 * <devpath>sromrev's existance in flash to decide if we should return an error when CRC
	 * fails or read SROM variables from flash.
	 */

	/* freed in same function */
	srom = MALLOC_NOPERSIST(osh, SROM_MAX);
	ASSERT(srom != NULL);
	if (!srom)
		return -2;

	sromwindow = (volatile uint16 *)srom_offset(sih, curmap);
	if (si_is_sprom_available(sih)) {
		err = sprom_read_pci(osh, sih, sromwindow, 0, srom, SROM_SIGN_MINWORDS + 1, FALSE);
		if (err == 0) {
			if (srom[SROM18_SIGN] == SROM18_SIGNATURE) {
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM18_WORDS, TRUE);
				sromrev = srom[SROM18_CRCREV] & 0xff;
			} else if (srom[SROM17_SIGN] == SROM17_SIGNATURE) {
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM17_WORDS, TRUE);
				sromrev = srom[SROM17_CRCREV] & 0xff;
			} else if (srom[SROM16_SIGN] == SROM16_SIGNATURE) {
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM16_WORDS, TRUE);
				sromrev = srom[SROM16_CRCREV] & 0xff;
			} else if (srom[SROM15_SIGN] == SROM15_SIGNATURE) { /* srom 15  */
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM15_WORDS, TRUE);
				sromrev = srom[SROM15_CRCREV] & 0xff;
			} else if (srom[SROM11_SIGN] == SROM13_SIGNATURE) {
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM13_WORDS, TRUE);
				sromrev = srom[SROM13_CRCREV] & 0xff;
			} else if (srom[SROM11_SIGN] == SROM12_SIGNATURE) {
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM12_WORDS, TRUE);
				sromrev = srom[SROM12_CRCREV] & 0xff;
			} else if (srom[SROM11_SIGN] == SROM11_SIGNATURE) {
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM11_WORDS, TRUE);
				sromrev = srom[SROM11_CRCREV] & 0xff;
			} else  if ((srom[SROM4_SIGN] == SROM4_SIGNATURE) || /* srom 4  */
				(srom[SROM8_SIGN] == SROM4_SIGNATURE)) { /* srom 8,9  */
				err = sprom_read_pci(osh, sih, sromwindow,
						0, srom, SROM4_WORDS, TRUE);
				sromrev = srom[SROM4_CRCREV] & 0xff;
			} else {
				err = sprom_read_pci(osh, sih, sromwindow, 0,
						srom, SROM_WORDS, TRUE);
				if (err == 0) {
					/* srom is good and is rev < 4 */
					/* top word of sprom contains version and crc8 */
					sromrev = srom[SROM_CRCREV] & 0xff;
					/* bcm4401 sroms misprogrammed */
					if (sromrev == 0x10)
						sromrev = 1;
				}
			}
			if (err)
				BS_ERROR(("srom read failed\n"));
		}
		else
			BS_ERROR(("srom read failed\n"));
	}

#if defined(BCMNVRAMW) || defined(BCMNVRAMR)
	/* Use OTP if SPROM not available */
	else if ((err = otp_read_pci(osh, sih, srom, SROM_MAX)) == 0) {
		/* OTP only contain SROM rev8/rev9/rev10/Rev11 for now */

		if (srom[SROM13_SIGN] == SROM13_SIGNATURE)
			sromrev = srom[SROM13_CRCREV] & 0xff;
		else if (srom[SROM12_SIGN] == SROM12_SIGNATURE)
			sromrev = srom[SROM12_CRCREV] & 0xff;
		else if (srom[SROM11_SIGN] == SROM11_SIGNATURE)
			sromrev = srom[SROM11_CRCREV] & 0xff;
		else if (srom[SROM10_SIGN] == SROM10_SIGNATURE)
			sromrev = srom[SROM10_CRCREV] & 0xff;
		else
			sromrev = srom[SROM4_CRCREV] & 0xff;
	}
#endif /* defined(BCMNVRAMW) || defined(BCMNVRAMR) */
	else {
		err = 1;
		BS_ERROR(("Neither SPROM nor OTP has valid image\n"));
	}

	BS_ERROR(("srom rev:%d\n", sromrev));

	/* We want internal/wltest driver to come up with default sromvars so we can
	 * program a blank SPROM/OTP.
	 */
	if (err || sromrev == 0) {
		char *value;
#if defined(BCMHOSTVARS)
		uint32 val;
#endif

		if ((value = si_getdevpathvar(sih, "sromrev"))) {
			sromrev = (uint8)bcm_strtoul(value, NULL, 0);
			flash = TRUE;
			goto varscont;
		}

		BS_ERROR(("initvars_srom_pci, SROM CRC Error\n"));

#if !defined(DONGLEBUILD) || defined(BCMPCIEDEV_SROM_FORMAT)
		/* NIC build or PCIe FD using SROM format shouldn't load driver
		 * default when external nvram exists.
		 */
		if ((value = getvar(NULL, "sromrev"))) {
			BS_ERROR(("initvars_srom_pci, Using external nvram\n"));
			err = 0;
			goto errout;
		}
#endif /* !DONGLEBUILD || BCMPCIEDEV_SROM_FORMAT */

#if defined(BCMHOSTVARS)
		/*
		 * CRC failed on srom, so if the device is using OTP
		 * and if OTP is not programmed use the default variables.
		 * for 4311 A1 there is no signature to indicate that OTP is
		 * programmed, so can't really verify the OTP is unprogrammed
		 * or a bad OTP.
		*/
		val = OSL_PCI_READ_CONFIG(osh, PCI_SPROM_CONTROL, sizeof(uint32));
		if ((si_is_sprom_available(sih) && srom[0] == 0xffff) ||
#ifdef BCMQT
			(si_is_sprom_available(sih) && sromrev == 0) ||
#endif
			(val & SPROM_OTPIN_USE)) {
			vp = base = mfgsromvars;

			/* For windows internal/wltest driver, a .nvm file with default
			 * nvram parameters is downloaded from the file system (in src/wl/sys:
			 * wl_readconfigdata()).
			 * Only when we cannot download default vars from the file system, use
			 * defaultsromvars_wltest as default
			 */
			if (defvarslen == 0) {
				BS_ERROR(("No nvm file, use generic default (for programming"
					" SPROM/OTP only)\n"));

				if (BCM43602_CHIP(sih->chip)) {
					defvarslen = srom_vars_len(defaultsromvars_43602);
					bcopy(defaultsromvars_43602, vp, defvarslen);
				} else if ((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
				           (CHIPID(sih->chip) == BCM4352_CHIP_ID)) {
					defvarslen = srom_vars_len(defaultsromvars_4360);
					bcopy(defaultsromvars_4360, vp, defvarslen);
				} else if (BCM4378_CHIP(sih->chip)) {
					defvarslen = srom_vars_len(defaultsromvars_4378);
					bcopy(defaultsromvars_4378, vp, defvarslen);
				} else if (BCM4387_CHIP(sih->chip)) {
					defvarslen = srom_vars_len(defaultsromvars_4387);
					bcopy(defaultsromvars_4387, vp, defvarslen);
				} else {
					defvarslen = srom_vars_len(defaultsromvars_wltest);
					bcopy(defaultsromvars_wltest, vp, defvarslen);
				}
			} else {
				BS_ERROR(("Use nvm file as default\n"));
			}

			vp += defvarslen;
			/* add final null terminator */
			*vp++ = '\0';

			BS_ERROR(("Used %d bytes of defaultsromvars\n", defvarslen));
			goto varsdone;

		} else if ((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
			(CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
			(CHIPID(sih->chip) == BCM4352_CHIP_ID) ||
			BCM43602_CHIP(sih->chip)) {

			base = vp = mfgsromvars;

			if ((CHIPID(sih->chip) == BCM4360_CHIP_ID) ||
			    (CHIPID(sih->chip) == BCM43460_CHIP_ID) ||
			    (CHIPID(sih->chip) == BCM4352_CHIP_ID) ||
			    BCM43602_CHIP(sih->chip))
				BS_ERROR(("4360 BOOT w/o SPROM or OTP\n"));
			else
				BS_ERROR(("BOOT w/o SPROM or OTP\n"));

			if (defvarslen == 0) {
				if (BCM43602_CHIP(sih->chip)) {
					defvarslen = srom_vars_len(defaultsromvars_43602);
					bcopy(defaultsromvars_43602, vp, defvarslen);
				} else if ((sih->chip == BCM4360_CHIP_ID) ||
						(sih->chip == BCM4352_CHIP_ID)) {
					defvarslen = srom_vars_len(defaultsromvars_4360);
					bcopy(defaultsromvars_4360, vp, defvarslen);
				} else {
					defvarslen = srom_vars_len(defaultsromvars_4331);
					bcopy(defaultsromvars_4331, vp, defvarslen);
				}
			}
			vp += defvarslen;
			*vp++ = '\0';
			goto varsdone;
		} else
#endif /* defined(BCMHOSTVARS) */
		{
			err = -1;
			goto errout;
		}
	}
#if defined(BCM_ONE_NVRAM_SRC)
	/* Discard hostvars if SROM parsing is successful, so only one nvram source
	 * will be used.
	 * Routers use combined srom/host nvram so shouldn't define BCM_ONE_NVRAM_SRC.
	 */
	else {
		nvram_exit((void *)sih); /* free up global vars */
	}
#endif /* BCM_ONE_NVRAM_SRC */

varscont:
	/* Bitmask for the sromrev */
	sr = 1 << sromrev;

	/* srom version check: Current valid versions are:
	  * 1-5, 8-11, 12, 13, 15, 16, 17, 18 SROM_MAXREV
	  * This is a bit mask of all valid SROM versions.
	  */
	if ((sr & 0x7bf3e) == 0) {
		BS_ERROR(("Invalid SROM rev %d\n", sromrev));
		err = -2;
		goto errout;
	}

	ASSERT(vars != NULL);
	ASSERT(count != NULL);

#if defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL)
	srom_sromrev = sromrev;
#endif /* defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL) */

	/* freed in same function */
	base = vp = MALLOC_NOPERSIST(osh, MAXSZ_NVRAM_VARS);
	ASSERT(vp != NULL);
	if (!vp) {
		err = -2;
		goto errout;
	}

	/* read variables from flash */
	if (flash) {
		if ((err = initvars_flash(sih, osh, &vp, MAXSZ_NVRAM_VARS)))
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
	err = initvars_table(osh, base, vp, vars, count); /* allocates buffer in 'vars' */

#if defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL)
	if (sromrev == 18) {
		int caldata_wordoffset = srom[SROM18_CALDATA_OFFSET_LOC] / 2;

		if ((caldata_wordoffset != 0) &&
			(caldata_wordoffset + SROM_CALDATA_WORDS < SROM18_WORDS)) {
			memcpy(caldata_array, srom + caldata_wordoffset, SROM18_CALDATA_WORDS * 2);
			is_caldata_prsnt = TRUE;
		}
	} else if (sromrev == 16) {
		int caldata_wordoffset = srom[SROM16_CALDATA_OFFSET_LOC] / 2;

		if ((caldata_wordoffset != 0) &&
			(caldata_wordoffset + SROM_CALDATA_WORDS < SROM16_WORDS)) {
			memcpy(caldata_array, srom + caldata_wordoffset, SROM_CALDATA_WORDS * 2);
			is_caldata_prsnt = TRUE;
		}
	}
#endif /* defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL) */

#ifdef NVSRCX
	if (sromrev != 0)
		nvram_append((void *)sih, *vars, *count, VARBUF_PRIO_SROM);
#endif
#if defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL)
	if ((sromrev == 15) || (sromrev == 17)) {
		uint32 caldata_size = get_srom_pci_caldata_size(sromrev);

		cal_wordoffset = getintvar(NULL, "caldata_offset")/2;
		memcpy(caldata_array, srom + cal_wordoffset, caldata_size);
	}
#endif
errout:
#if defined(BCMHOSTVARS)
	if (base && (base != mfgsromvars))
#else
	if (base)
#endif /* defined(BCMHOSTVARS) */
		MFREE(osh, base, MAXSZ_NVRAM_VARS);

	MFREE(osh, srom, SROM_MAX);
	return err;
}

/**
 * initvars_cis_pci() parses OTP CIS. This is specifically for PCIe full dongle that has SROM
 * header plus CIS tuples programmed in OTP.
 * Return error if the content is not in CIS format or OTP is not present.
 */
static int
BCMATTACHFN(initvars_cis_pci)(si_t *sih, osl_t *osh, volatile void *curmap,
	char **vars, uint *count)
{
	uint wsz = 0, sz = 0, base_len = 0;
	void *oh = NULL;
	int rc = BCME_OK;
	uint16 *cisbuf = NULL;
	uint8 *cis = NULL;
#if defined (BCMHOSTVARS)
	char *vp = NULL;
#endif /* BCMHOSTVARS */
	char *base = NULL;
	bool wasup;
	uint32 min_res_mask = 0;
	BCM_REFERENCE(curmap);

	/* Bail out if we've dealt with OTP/SPROM before! */
	if (srvars_inited)
		goto exit;

	/* Turn on OTP if it's not already on */
	if (!(wasup = si_is_otp_powered(sih)))
		si_otp_power(sih, TRUE, &min_res_mask);

	if (si_cis_source(sih) != CIS_OTP)
		rc = BCME_NOTFOUND;
	else if ((oh = otp_init(sih)) == NULL)
		rc = BCME_ERROR;
	else if (!(((BUSCORETYPE(sih->buscoretype) == PCIE2_CORE_ID) || otp_newcis(oh)) &&
		(otp_status(oh) & OTPS_GUP_HW))) {
		/* OTP bit CIS format (507) not used by pcie core - only needed for sdio core */
		rc = BCME_NOTFOUND;
	} else if ((sz = otp_size(oh)) != 0) {
		if ((cisbuf = (uint16*)MALLOC_NOPERSIST(osh, sz))) {
			/* otp_size() returns bytes, not words. */
			wsz = sz >> 1;
			/* for 4389b0 (CCREV-70) sw region is before the hw region */
			if (CCREV(sih->ccrev) == 70) {
				rc = otp_read_region(sih, OTP_SW_RGN, cisbuf, &wsz);
				cis = (uint8*)cisbuf;
			} else {
				rc = otp_read_region(sih, OTP_HW_RGN, cisbuf, &wsz);
				/* Bypass the HW header and signature */
				cis = (uint8*)(cisbuf + (otp_pcie_hwhdr_sz(sih) / 2));
			}
			BS_ERROR(("initvars_cis_pci: Parsing CIS in OTP.\n"));
		} else
			rc = BCME_NOMEM;
	}

	/* Restore original OTP state */
	if (!wasup)
		si_otp_power(sih, FALSE, &min_res_mask);

	if (rc != BCME_OK) {
		BS_ERROR(("initvars_cis_pci: Not CIS format\n"));
		goto exit;
	}

#if defined (BCMHOSTVARS)
	if (defvarslen) {
		vp = mfgsromvars;
		vp += defvarslen;

		/* allocates buffer in 'vars' */
		rc = initvars_table(osh, mfgsromvars, vp, &base, &base_len);
		if (rc)
			goto exit;

		*vars = base;
		*count = base_len;

		BS_ERROR(("initvars_cis_pci external nvram %d bytes\n", defvarslen));
	}

#endif /* BCMHOSTVARS */

	/* Parse the CIS and allocate a(nother) buffer in 'vars' */
	rc = srom_parsecis(sih, osh, &cis, SROM_CIS_SINGLE, vars, count);

	srvars_inited = TRUE;
exit:
	/* Clean up */
	if (base)
		MFREE(osh, base, base_len);
	if (cisbuf)
		MFREE(osh, cisbuf, sz);

	/* return OK so the driver will load & use defaults if bad srom/otp */
	return rc;
}
#endif /* !defined(BCMDONGLEHOST) */

#ifdef BCMSDIO
#if !defined(BCMDONGLEHOST)
/**
 * Read the SDIO cis and call parsecis to allocate and initialize the NVRAM vars buffer.
 * Return 0 on success, nonzero on error.
 */
static int
BCMATTACHFN(initvars_cis_sdio)(si_t *sih, osl_t *osh, char **vars, uint *count)
{
	uint8 *cis[SBSDIO_NUM_FUNCTION + 1];
	uint fn, numfn;
	int rc = 0;

	/* Using MALLOC here causes the Windows driver to crash Needs Investigating */
#ifdef NDIS
	uint8 cisd[SBSDIO_NUM_FUNCTION + 1][SBSDIO_CIS_SIZE_LIMIT];
#endif

	numfn = bcmsdh_query_iofnum(NULL);
	ASSERT(numfn <= SDIOD_MAX_IOFUNCS);

	for (fn = 0; fn <= numfn; fn++) {
#ifdef NDIS
		cis[fn] = (uint8*)cisd[fn];
#else
		/* freed in same function */
		if ((cis[fn] = MALLOC_NOPERSIST(osh, SBSDIO_CIS_SIZE_LIMIT)) == NULL) {
			rc = -1;
			break;
		}
#endif /* NDIS */

		bzero(cis[fn], SBSDIO_CIS_SIZE_LIMIT);

		if (bcmsdh_cis_read(NULL, fn, cis[fn], SBSDIO_CIS_SIZE_LIMIT) != 0) {
#ifdef NDIS
			/* nothing to do */
#else
			MFREE(osh, cis[fn], SBSDIO_CIS_SIZE_LIMIT);
#endif
			rc = -2;
			break;
		}
	}

	if (!rc)
		rc = srom_parsecis(sih, osh, cis, fn, vars, count);

#ifdef NDIS
	/* nothing to do here */
#else
	while (fn-- > 0)
		MFREE(osh, cis[fn], SBSDIO_CIS_SIZE_LIMIT);
#endif

	return (rc);
}
#endif /* !defined(BCMDONGLEHOST) */

/** set SDIO sprom command register */
static int
BCMATTACHFN(sprom_cmd_sdio)(osl_t *osh, uint8 cmd)
{
	uint8 status = 0;
	uint wait_cnt = 1000;

	/* write sprom command register */
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_CS, cmd, NULL);

	/* wait status */
	while (wait_cnt--) {
		status = bcmsdh_cfg_read(NULL, SDIO_FUNC_1, SBSDIO_SPROM_CS, NULL);
		if (status & SBSDIO_SPROM_DONE)
			return 0;
	}

	return 1;
}

/** read a word from the SDIO srom */
static int
sprom_read_sdio(osl_t *osh, uint16 addr, uint16 *data)
{
	uint8 addr_l, addr_h, data_l, data_h;

	addr_l = (uint8)((addr * 2) & 0xff);
	addr_h = (uint8)(((addr * 2) >> 8) & 0xff);

	/* set address */
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_ADDR_HIGH, addr_h, NULL);
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_ADDR_LOW, addr_l, NULL);

	/* do read */
	if (sprom_cmd_sdio(osh, SBSDIO_SPROM_READ))
		return 1;

	/* read data */
	data_h = bcmsdh_cfg_read(NULL, SDIO_FUNC_1, SBSDIO_SPROM_DATA_HIGH, NULL);
	data_l = bcmsdh_cfg_read(NULL, SDIO_FUNC_1, SBSDIO_SPROM_DATA_LOW, NULL);

	*data = (data_h << 8) | data_l;
	return 0;
}

#if defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG)
/** write a word to the SDIO srom */
static int
sprom_write_sdio(osl_t *osh, uint16 addr, uint16 data)
{
	uint8 addr_l, addr_h, data_l, data_h;

	addr_l = (uint8)((addr * 2) & 0xff);
	addr_h = (uint8)(((addr * 2) >> 8) & 0xff);
	data_l = (uint8)(data & 0xff);
	data_h = (uint8)((data >> 8) & 0xff);

	/* set address */
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_ADDR_HIGH, addr_h, NULL);
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_ADDR_LOW, addr_l, NULL);

	/* write data */
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_DATA_HIGH, data_h, NULL);
	bcmsdh_cfg_write(NULL, SDIO_FUNC_1, SBSDIO_SPROM_DATA_LOW, data_l, NULL);

	/* do write */
	return sprom_cmd_sdio(osh, SBSDIO_SPROM_WRITE);
}
#endif /* defined(WLTEST) || defined (DHD_SPROM) || defined (BCMDBG) */
#endif /* BCMSDIO */

#if !defined(BCMDONGLEHOST)
#ifdef BCMSPI
/**
 * Read the SPI cis and call parsecis to allocate and initialize the NVRAM vars buffer.
 * Return 0 on success, nonzero on error.
 */
static int
BCMATTACHFN(initvars_cis_spi)(si_t *sih, osl_t *osh, char **vars, uint *count)
{
	uint8 *cis;
	int rc;

	/* Using MALLOC here causes the Windows driver to crash Needs Investigating */
#ifdef NDIS
	uint8 cisd[SBSDIO_CIS_SIZE_LIMIT];
	cis = (uint8*)cisd;
#else
	/* freed in same function */
	if ((cis = MALLOC_NOPERSIST(osh, SBSDIO_CIS_SIZE_LIMIT)) == NULL) {
		return -1;
	}
#endif /* NDIS */

	bzero(cis, SBSDIO_CIS_SIZE_LIMIT);

	if (bcmsdh_cis_read(NULL, SDIO_FUNC_1, cis, SBSDIO_CIS_SIZE_LIMIT) != 0) {
#ifdef NDIS
		/* nothing to do */
#else
		MFREE(osh, cis, SBSDIO_CIS_SIZE_LIMIT);
#endif /* NDIS */
		return -2;
	}

	rc = srom_parsecis(sih, osh, &cis, SDIO_FUNC_1, vars, count);

#ifdef NDIS
	/* nothing to do here */
#else
	MFREE(osh, cis, SBSDIO_CIS_SIZE_LIMIT);
#endif

	return (rc);
}
#endif /* BCMSPI */
#endif /* !defined(BCMDONGLEHOST) */

/** Return sprom size in 16-bit words */
uint
srom_size(si_t *sih, osl_t *osh)
{
	uint size = (SROM16_SIGN + 1) * 2;	/* must big enough for SROM16 */
	return size;
}

/**
 * initvars are different for BCMUSBDEV and BCMSDIODEV.  This is OK when supporting both at
 * the same time, but only because all of the code is in attach functions and not in ROM.
 */

#if defined(BCMUSBDEV_ENABLED)
#ifdef BCM_DONGLEVARS
/*** reads a CIS structure (so not an SROM-MAP structure) from either OTP or SROM */
static int
BCMATTACHFN(initvars_srom_si_bl)(si_t *sih, osl_t *osh, volatile void *curmap,
	char **vars, uint *varsz)
{
	int sel = 0;		/* where to read srom/cis: 0 - none, 1 - otp, 2 - sprom */
	uint sz = 0;		/* srom size in bytes */
	void *oh = NULL;
	int rc = BCME_OK;
	uint16 prio = VARBUF_PRIO_INVALID;

	if ((oh = otp_init(sih)) != NULL && (otp_status(oh) & OTPS_GUP_SW)) {
		/* Access OTP if it is present, powered on, and programmed */
		sz = otp_size(oh);
		sel = 1;
	} else if ((sz = srom_size(sih, osh)) != 0) {
		/* Access the SPROM if it is present */
		sz <<= 1;
		sel = 2;
	}

	/* Read CIS in OTP/SPROM */
	if (sel != 0) {
		uint16 *srom;
		uint8 *body = NULL;
		uint otpsz = sz;

		ASSERT(sz);

		/* Allocate memory */
		if ((srom = (uint16 *)MALLOC(osh, sz)) == NULL)
			return BCME_NOMEM;

		/* Read CIS */
		switch (sel) {
		case 1:
			rc = otp_read_region(sih, OTP_SW_RGN, srom, &otpsz);
			sz = otpsz;
			body = (uint8 *)srom;
			prio = VARBUF_PRIO_OTP;
			break;
		case 2:
			rc = srom_read(sih, SI_BUS, curmap, osh, 0, sz, srom, TRUE);
			/* sprom has 8 byte h/w header */
			body = (uint8 *)srom + SBSDIO_SPROM_CIS_OFFSET;
			prio = VARBUF_PRIO_SROM;
			break;
		default:
			/* impossible to come here */
			ASSERT(0);
			break;
		}

		/* Parse CIS */
		if (rc == BCME_OK) {
			/* each word is in host endian */
			htol16_buf((uint8 *)srom, sz);
			ASSERT(body);
			rc = srom_parsecis(sih, osh, &body, SROM_CIS_SINGLE, vars, varsz);
		}

		MFREE(osh, srom, sz);	/* Clean up */

		/* Make SROM variables global */
		if (rc == BCME_OK) {
			nvram_append((void *)sih, *vars, *varsz, prio);
			DONGLE_STORE_VARS_OTP_PTR(*vars);
		}
	}

	return BCME_OK;
}
#endif	/* #ifdef BCM_DONGLEVARS */

/**
 * initvars_srom_si() is defined multiple times in this file. This is the 1st variant for chips with
 * an active USB interface. It is called only for bus types SI_BUS, and only for CIS
 * format in SPROM and/or OTP. Reads OTP or SPROM (bootloader only) and appends parsed contents to
 * caller supplied var/value pairs.
 */
static int
BCMATTACHFN(initvars_srom_si)(si_t *sih, osl_t *osh, volatile void *curmap,
	char **vars, uint *varsz)
{

#if defined(BCM_DONGLEVARS)
	BCM_REFERENCE(osh);
	BCM_REFERENCE(sih);
	BCM_REFERENCE(curmap);
#endif

	/* Bail out if we've dealt with OTP/SPROM before! */
	if (srvars_inited)
		goto exit;

#ifdef BCM_DONGLEVARS	/* this flag should be defined for usb bootloader, to read OTP or SROM */
	if (BCME_OK != initvars_srom_si_bl(sih, osh, curmap, vars, varsz)) /* CIS format only */
		return BCME_ERROR;
#endif

	/* update static local var to skip for next call */
	srvars_inited = TRUE;

exit:
	/* Tell the caller there is no individual SROM variables */
	*vars = NULL;
	*varsz = 0;

	/* return OK so the driver will load & use defaults if bad srom/otp */
	return BCME_OK;
}

#elif defined(BCMSDIODEV_ENABLED)

#ifdef BCM_DONGLEVARS
static uint8 BCMATTACHDATA(defcis4369)[] = { 0x20, 0x4, 0xd0, 0x2, 0x64, 0x43, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis43012)[] = { 0x20, 0x4, 0xd0, 0x2, 0x04, 0xA8, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis43013)[] = { 0x20, 0x4, 0xd0, 0x2, 0x05, 0xA8, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis43014)[] = { 0x20, 0x4, 0xd0, 0x2, 0x06, 0xA8, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis4362)[] = { 0x20, 0x4, 0xd0, 0x2, 0x62, 0x43, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis4378)[] = { 0x20, 0x4, 0xd0, 0x2, 0x78, 0x43, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis4385)[] = { 0x20, 0x4, 0xd0, 0x2, 0x85, 0x43, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis4387)[] = { 0x20, 0x4, 0xd0, 0x2, 0x78, 0x43, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis4388)[] = { 0x20, 0x4, 0xd0, 0x2, 0x88, 0x43, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis4389)[] = { 0x20, 0x4, 0xd0, 0x2, 0x89, 0x43, 0xff, 0xff };
static uint8 BCMATTACHDATA(defcis4397)[] = { 0x20, 0x4, 0xd0, 0x2, 0x97, 0x43, 0xff, 0xff };

/**
 * initvars_srom_si() is defined multiple times in this file. This is the 2nd variant for chips with
 * an active SDIOd interface using DONGLEVARS
 */
static int
BCMATTACHFN(initvars_srom_si)(si_t *sih, osl_t *osh, volatile void *curmap,
	char **vars, uint *varsz)
{
	int cis_src;
	uint msz = 0;
	uint sz = 0;
	void *oh = NULL;
	int rc = BCME_OK;
	bool	new_cisformat = FALSE;

	uint16 *cisbuf = NULL;

	/* # sdiod fns + common + extra */
	uint8 *cis[SBSDIO_NUM_FUNCTION + 2] = { 0 };

	uint ciss = 0;
	uint8 *defcis;
	uint hdrsz;
	uint16 prio = VARBUF_PRIO_INVALID;

#if defined(BCMSDIODEV_ENABLED) && defined(ATE_BUILD)
	if (si_chipcap_sdio_ate_only(sih)) {
		BS_ERROR(("ATE BUILD: skip cis based var init\n"));
		goto exit;
	}
#endif /* BCMSDIODEV_ENABLED && ATE_BUILD */

	/* Bail out if we've dealt with OTP/SPROM before! */
	if (srvars_inited)
		goto exit;

	/* Initialize default and cis format count */
	switch (CHIPID(sih->chip)) {
	case BCM4369_CHIP_GRPID: ciss = 1; defcis = defcis4369; hdrsz = 4; break;
	case BCM4378_CHIP_GRPID: ciss = 1; defcis = defcis4378; hdrsz = 4; break;
	case BCM4385_CHIP_GRPID: ciss = 1; defcis = defcis4385; hdrsz = 4; break;
	case BCM4387_CHIP_GRPID: ciss = 1; defcis = defcis4387; hdrsz = 4; break;
	case BCM4388_CHIP_GRPID: ciss = 1; defcis = defcis4388; hdrsz = 4; break;
	case BCM4389_CHIP_GRPID: ciss = 1; defcis = defcis4389; hdrsz = 4; break;
	case BCM4397_CHIP_GRPID: ciss = 1; defcis = defcis4397; hdrsz = 4; break;
	case BCM43012_CHIP_ID: ciss = 1; defcis = defcis43012; hdrsz = 4; break;
	case BCM43013_CHIP_ID: ciss = 1; defcis = defcis43013; hdrsz = 4; break;
	case BCM43014_CHIP_ID: ciss = 1; defcis = defcis43014; hdrsz = 4; break;
	case BCM4362_CHIP_GRPID: ciss = 1; defcis = defcis4362; hdrsz = 4; break;
	default:
		BS_ERROR(("initvars_srom_si: Unknown chip 0x%04x\n", CHIPID(sih->chip)));
		return BCME_ERROR;
	}
	if (sih->ccrev >= 36) {
		uint32 otplayout;
		if (AOB_ENAB(sih)) {
			otplayout = si_corereg(sih, si_findcoreidx(sih, GCI_CORE_ID, 0),
			 OFFSETOF(gciregs_t, otplayout), 0, 0);
		} else {
			otplayout = si_corereg(sih, SI_CC_IDX, OFFSETOF(chipcregs_t, otplayout),
			 0, 0);
		}
		if (otplayout & OTP_CISFORMAT_NEW) {
			ciss = 1;
			hdrsz = 2;
			new_cisformat = TRUE;
		}
		else {
			ciss = 3;
			hdrsz = 12;
		}
	}

	cis_src = si_cis_source(sih);
	switch (cis_src) {
	case CIS_SROM:
		sz = srom_size(sih, osh) << 1;
		prio = VARBUF_PRIO_SROM;
		break;
	case CIS_OTP:
		/* Note that for *this* type of OTP -- which otp_read_region()
		 * can operate on -- otp_size() returns bytes, not words.
		 */
		if (((oh = otp_init(sih)) != NULL) && (otp_status(oh) & OTPS_GUP_HW))
			sz = otp_size(oh);
		prio = VARBUF_PRIO_OTP;
		break;
	}

	if (sz != 0) {
		/* freed in same function */
		if ((cisbuf = (uint16*)MALLOC_NOPERSIST(osh, sz)) == NULL)
			return BCME_NOMEM;
		msz = sz;

		switch (cis_src) {
		case CIS_SROM:
			rc = srom_read(sih, SI_BUS, curmap, osh, 0, sz, cisbuf, FALSE);
			break;
		case CIS_OTP:
			sz >>= 1;
			rc = otp_read_region(sih, OTP_HW_RGN, cisbuf, &sz);
			sz <<= 1;
			break;
		}

		ASSERT(sz > hdrsz);
		if (rc == BCME_OK) {
			if ((cisbuf[0] == 0xffff) || (cisbuf[0] == 0)) {
				MFREE(osh, cisbuf, msz);
			} else if (new_cisformat) {
				cis[0] = (uint8*)(cisbuf + hdrsz);
			} else {
				cis[0] = (uint8*)cisbuf + hdrsz;
				cis[1] = (uint8*)cisbuf + hdrsz +
				        (cisbuf[1] >> 8) + ((cisbuf[2] & 0x00ff) << 8) -
				        SBSDIO_CIS_BASE_COMMON;
				cis[2] = (uint8*)cisbuf + hdrsz +
				        cisbuf[3] - SBSDIO_CIS_BASE_COMMON;
				cis[3] = (uint8*)cisbuf + hdrsz +
				        cisbuf[4] - SBSDIO_CIS_BASE_COMMON;
				ASSERT((cis[1] >= cis[0]) && (cis[1] < (uint8*)cisbuf + sz));
				ASSERT((cis[2] >= cis[0]) && (cis[2] < (uint8*)cisbuf + sz));
				ASSERT(((cis[3] >= cis[0]) && (cis[3] < (uint8*)cisbuf + sz)) ||
				        (ciss <= 3));
			}
		}
	}

	/* Use default if strapped to, or strapped source empty */
	if (cisbuf == NULL) {
		ciss = 1;
		cis[0] = defcis;
	}

	/* Parse the CIS */
	if (rc == BCME_OK) {
		if ((rc = srom_parsecis(sih, osh, cis, ciss, vars, varsz)) == BCME_OK) {
			nvram_append((void *)sih, *vars, *varsz, prio);
			DONGLE_STORE_VARS_OTP_PTR(*vars);
		}
	}

	/* Clean up */
	if (cisbuf != NULL)
		MFREE(osh, cisbuf, msz);

	srvars_inited = TRUE;
exit:
	/* Tell the caller there is no individual SROM variables */
	*vars = NULL;
	*varsz = 0;

	/* return OK so the driver will load & use defaults if bad srom/otp */
	return BCME_OK;
} /* initvars_srom_si */
#else /* BCM_DONGLEVARS */

/**
 * initvars_srom_si() is defined multiple times in this file. This is the variant for chips with an
 * active SDIOd interface but without BCM_DONGLEVARS
 */
static int
BCMATTACHFN(initvars_srom_si)(si_t *sih, osl_t *osh, volatile void *curmap,
	char **vars, uint *varsz)
{
	*vars = NULL;
	*varsz = 0;
	return BCME_OK;
}
#endif /* BCM_DONGLEVARS */

#elif defined(BCMPCIEDEV_ENABLED)

/**
 * initvars_srom_si() is defined multiple times in this file. This is the variant for chips with an
 * active PCIe interface *and* that use OTP for NVRAM storage.
 *
 * On success, a buffer containing var/val values has been allocated in parameter 'vars'.
 * put an ifdef where if the host wants the dongle wants to parse sprom or not
 */
static int
BCMATTACHFN(initvars_srom_si)(si_t *sih, osl_t *osh, volatile void *curmap,
	char **vars, uint *varsz)
{
#ifdef BCM_DONGLEVARS
	void *oh = NULL;
	uint8 *cis;
	uint sz = 0;
	int rc;

	if (si_cis_source(sih) !=  CIS_OTP)
		return BCME_OK;

	if (((oh = otp_init(sih)) != NULL) && (otp_status(oh) & OTPS_GUP_HW))
		sz = otp_size(oh);
	if (sz == 0)
		return BCME_OK;

	if ((cis = MALLOC(osh, sz)) == NULL)
		return BCME_NOMEM;
	sz >>= 1;
	rc = otp_read_region(sih, OTP_HW_RGN, (uint16 *)cis, &sz);
	sz <<= 1;

	/* account for the Hardware header */
	if (sz == 128)
		return BCME_OK;

	cis += 128;

	/* need to find a better way to identify sprom format content and ignore parse */
	if (*(uint16 *)cis == SROM11_SIGNATURE) {
		return BCME_OK;
	}

	if ((rc = srom_parsecis(sih, osh, &cis, SROM_CIS_SINGLE, vars, varsz)) == BCME_OK)
		nvram_append((void *)sih, *vars, *varsz, VARBUF_PRIO_OTP);

	return rc;
#else /* BCM_DONGLEVARS */
	*vars = NULL;
	*varsz = 0;
	return BCME_OK;
#endif /* BCM_DONGLEVARS */
}
#else /* !BCMUSBDEV && !BCMSDIODEV  && !BCMPCIEDEV */

#ifndef BCMDONGLEHOST

/**
 * initvars_srom_si() is defined multiple times in this file. This is the variant for:
 * !BCMDONGLEHOST && !BCMUSBDEV && !BCMSDIODEV && !BCMPCIEDEV
 * So this function is defined for PCI (not PCIe) builds that are also non DHD builds.
 * On success, a buffer containing var/val values has been allocated in parameter 'vars'.
 */
static int
BCMATTACHFN(initvars_srom_si)(si_t *sih, osl_t *osh, volatile void *curmap,
	char **vars, uint *varsz)
{
	/* Search flash nvram section for srom variables */
	BCM_REFERENCE(osh);
	BCM_REFERENCE(curmap);
	return initvars_flash_si(sih, vars, varsz);
} /* initvars_srom_si */
#endif /* !BCMDONGLEHOST */
#endif	/* !BCMUSBDEV && !BCMSDIODEV  && !BCMPCIEDEV */

void
BCMATTACHFN(srom_var_deinit)(si_t *sih)
{
	BCM_REFERENCE(sih);

	srvars_inited = FALSE;
}

#if defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL)
bool
BCMATTACHFN(srom_caldata_prsnt)(si_t *sih)
{
	return is_caldata_prsnt;
}

int
BCMATTACHFN(srom_get_caldata)(si_t *sih, uint16 *srom)
{
	if (!is_caldata_prsnt) {
		return BCME_ERROR;
	}
	if (srom_sromrev == 18) {
		memcpy(srom, caldata_array, SROM18_CALDATA_WORDS * 2);
	} else {
		memcpy(srom, caldata_array, SROM_CALDATA_WORDS * 2);
	}
	return BCME_OK;
}
#endif /* defined(BCMPCIEDEV_SROM_FORMAT) && defined(WLC_TXCAL) */
