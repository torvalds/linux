/* vi: set sw=4 ts=4: */
/*
 * hdparm implementation for busybox
 *
 * Copyright (C) [2003] by [Matteo Croce] <3297627799@wind.it>
 * Hacked by Tito <farmatito@tiscali.it> for size optimization.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * This program is based on the source code of hdparm: see below...
 * hdparm.c - Command line interface to get/set hard disk parameters
 *          - by Mark Lord (C) 1994-2002 -- freely distributable
 */
//config:config HDPARM
//config:	bool "hdparm (23 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Get/Set hard drive parameters. Primarily intended for ATA
//config:	drives.
//config:
//config:config FEATURE_HDPARM_GET_IDENTITY
//config:	bool "Support obtaining detailed information directly from drives"
//config:	default y
//config:	depends on HDPARM
//config:	help
//config:	Enable the -I and -i options to obtain detailed information
//config:	directly from drives about their capabilities and supported ATA
//config:	feature set. If no device name is specified, hdparm will read
//config:	identify data from stdin. Enabling this option will add about 16k...
//config:
//config:config FEATURE_HDPARM_HDIO_SCAN_HWIF
//config:	bool "Register an IDE interface (DANGEROUS)"
//config:	default y
//config:	depends on HDPARM
//config:	help
//config:	Enable the 'hdparm -R' option to register an IDE interface.
//config:	This is dangerous stuff, so you should probably say N.
//config:
//config:config FEATURE_HDPARM_HDIO_UNREGISTER_HWIF
//config:	bool "Un-register an IDE interface (DANGEROUS)"
//config:	default y
//config:	depends on HDPARM
//config:	help
//config:	Enable the 'hdparm -U' option to un-register an IDE interface.
//config:	This is dangerous stuff, so you should probably say N.
//config:
//config:config FEATURE_HDPARM_HDIO_DRIVE_RESET
//config:	bool "Perform device reset (DANGEROUS)"
//config:	default y
//config:	depends on HDPARM
//config:	help
//config:	Enable the 'hdparm -w' option to perform a device reset.
//config:	This is dangerous stuff, so you should probably say N.
//config:
//config:config FEATURE_HDPARM_HDIO_TRISTATE_HWIF
//config:	bool "Tristate device for hotswap (DANGEROUS)"
//config:	default y
//config:	depends on HDPARM
//config:	help
//config:	Enable the 'hdparm -x' option to tristate device for hotswap,
//config:	and the '-b' option to get/set bus state. This is dangerous
//config:	stuff, so you should probably say N.
//config:
//config:config FEATURE_HDPARM_HDIO_GETSET_DMA
//config:	bool "Get/set using_dma flag"
//config:	default y
//config:	depends on HDPARM
//config:	help
//config:	Enable the 'hdparm -d' option to get/set using_dma flag.

//applet:IF_HDPARM(APPLET(hdparm, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_HDPARM) += hdparm.o

//usage:#define hdparm_trivial_usage
//usage:       "[OPTIONS] [DEVICE]"
//usage:#define hdparm_full_usage "\n\n"
//usage:       "	-a	Get/set fs readahead"
//usage:     "\n	-A	Set drive read-lookahead flag (0/1)"
//usage:     "\n	-b	Get/set bus state (0 == off, 1 == on, 2 == tristate)"
//usage:     "\n	-B	Set Advanced Power Management setting (1-255)"
//usage:     "\n	-c	Get/set IDE 32-bit IO setting"
//usage:     "\n	-C	Check IDE power mode status"
//usage:	IF_FEATURE_HDPARM_HDIO_GETSET_DMA(
//usage:     "\n	-d	Get/set using_dma flag")
//usage:     "\n	-D	Enable/disable drive defect-mgmt"
//usage:     "\n	-f	Flush buffer cache for device on exit"
//usage:     "\n	-g	Display drive geometry"
//usage:     "\n	-h	Display terse usage information"
//usage:	IF_FEATURE_HDPARM_GET_IDENTITY(
//usage:     "\n	-i	Display drive identification")
//usage:	IF_FEATURE_HDPARM_GET_IDENTITY(
//usage:     "\n	-I	Detailed/current information directly from drive")
//usage:     "\n	-k	Get/set keep_settings_over_reset flag (0/1)"
//usage:     "\n	-K	Set drive keep_features_over_reset flag (0/1)"
//usage:     "\n	-L	Set drive doorlock (0/1) (removable harddisks only)"
//usage:     "\n	-m	Get/set multiple sector count"
//usage:     "\n	-n	Get/set ignore-write-errors flag (0/1)"
//usage:     "\n	-p	Set PIO mode on IDE interface chipset (0,1,2,3,4,...)"
//usage:     "\n	-P	Set drive prefetch count"
/* //usage:  "\n	-q	Change next setting quietly" - not supported ib bbox */
//usage:     "\n	-Q	Get/set DMA tagged-queuing depth (if supported)"
//usage:     "\n	-r	Get/set readonly flag (DANGEROUS to set)"
//usage:	IF_FEATURE_HDPARM_HDIO_SCAN_HWIF(
//usage:     "\n	-R	Register an IDE interface (DANGEROUS)")
//usage:     "\n	-S	Set standby (spindown) timeout"
//usage:     "\n	-t	Perform device read timings"
//usage:     "\n	-T	Perform cache read timings"
//usage:     "\n	-u	Get/set unmaskirq flag (0/1)"
//usage:	IF_FEATURE_HDPARM_HDIO_UNREGISTER_HWIF(
//usage:     "\n	-U	Unregister an IDE interface (DANGEROUS)")
//usage:     "\n	-v	Defaults; same as -mcudkrag for IDE drives"
//usage:     "\n	-V	Display program version and exit immediately"
//usage:	IF_FEATURE_HDPARM_HDIO_DRIVE_RESET(
//usage:     "\n	-w	Perform device reset (DANGEROUS)")
//usage:     "\n	-W	Set drive write-caching flag (0/1) (DANGEROUS)"
//usage:	IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF(
//usage:     "\n	-x	Tristate device for hotswap (0/1) (DANGEROUS)")
//usage:     "\n	-X	Set IDE xfer mode (DANGEROUS)"
//usage:     "\n	-y	Put IDE drive in standby mode"
//usage:     "\n	-Y	Put IDE drive to sleep"
//usage:     "\n	-Z	Disable Seagate auto-powersaving mode"
//usage:     "\n	-z	Reread partition table"

#include "libbb.h"
#include "common_bufsiz.h"
/* must be _after_ libbb.h: */
#include <linux/hdreg.h>
#include <sys/mount.h>
#if !defined(BLKGETSIZE64)
# define BLKGETSIZE64 _IOR(0x12,114,size_t)
#endif

/* device types */
/* ------------ */
#define NO_DEV                  0xffff
#define ATA_DEV                 0x0000
#define ATAPI_DEV               0x0001

/* word definitions */
/* ---------------- */
#define GEN_CONFIG		0   /* general configuration */
#define LCYLS			1   /* number of logical cylinders */
#define CONFIG			2   /* specific configuration */
#define LHEADS			3   /* number of logical heads */
#define TRACK_BYTES		4   /* number of bytes/track (ATA-1) */
#define SECT_BYTES		5   /* number of bytes/sector (ATA-1) */
#define LSECTS			6   /* number of logical sectors/track */
#define START_SERIAL            10  /* ASCII serial number */
#define LENGTH_SERIAL           10  /* 10 words (20 bytes or characters) */
#define BUF_TYPE		20  /* buffer type (ATA-1) */
#define BUFFER__SIZE		21  /* buffer size (ATA-1) */
#define RW_LONG			22  /* extra bytes in R/W LONG cmd ( < ATA-4)*/
#define START_FW_REV            23  /* ASCII firmware revision */
#define LENGTH_FW_REV		 4  /*  4 words (8 bytes or characters) */
#define START_MODEL		27  /* ASCII model number */
#define LENGTH_MODEL		20  /* 20 words (40 bytes or characters) */
#define SECTOR_XFER_MAX		47  /* r/w multiple: max sectors xfered */
#define DWORD_IO		48  /* can do double-word IO (ATA-1 only) */
#define CAPAB_0			49  /* capabilities */
#define CAPAB_1			50
#define PIO_MODE		51  /* max PIO mode supported (obsolete)*/
#define DMA_MODE		52  /* max Singleword DMA mode supported (obs)*/
#define WHATS_VALID		53  /* what fields are valid */
#define LCYLS_CUR		54  /* current logical cylinders */
#define LHEADS_CUR		55  /* current logical heads */
#define LSECTS_CUR		56  /* current logical sectors/track */
#define CAPACITY_LSB		57  /* current capacity in sectors */
#define CAPACITY_MSB		58
#define SECTOR_XFER_CUR		59  /* r/w multiple: current sectors xfered */
#define LBA_SECTS_LSB		60  /* LBA: total number of user */
#define LBA_SECTS_MSB		61  /*      addressable sectors */
#define SINGLE_DMA		62  /* singleword DMA modes */
#define MULTI_DMA		63  /* multiword DMA modes */
#define ADV_PIO_MODES		64  /* advanced PIO modes supported */
				    /* multiword DMA xfer cycle time: */
#define DMA_TIME_MIN		65  /*   - minimum */
#define DMA_TIME_NORM		66  /*   - manufacturer's recommended */
				    /* minimum PIO xfer cycle time: */
#define PIO_NO_FLOW		67  /*   - without flow control */
#define PIO_FLOW		68  /*   - with IORDY flow control */
#define PKT_REL			71  /* typical #ns from PKT cmd to bus rel */
#define SVC_NBSY		72  /* typical #ns from SERVICE cmd to !BSY */
#define CDR_MAJOR		73  /* CD ROM: major version number */
#define CDR_MINOR		74  /* CD ROM: minor version number */
#define QUEUE_DEPTH		75  /* queue depth */
#define MAJOR			80  /* major version number */
#define MINOR			81  /* minor version number */
#define CMDS_SUPP_0		82  /* command/feature set(s) supported */
#define CMDS_SUPP_1		83
#define CMDS_SUPP_2		84
#define CMDS_EN_0		85  /* command/feature set(s) enabled */
#define CMDS_EN_1		86
#define CMDS_EN_2		87
#define ULTRA_DMA		88  /* ultra DMA modes */
				    /* time to complete security erase */
#define ERASE_TIME		89  /*   - ordinary */
#define ENH_ERASE_TIME		90  /*   - enhanced */
#define ADV_PWR			91  /* current advanced power management level
				       in low byte, 0x40 in high byte. */
#define PSWD_CODE		92  /* master password revision code */
#define HWRST_RSLT		93  /* hardware reset result */
#define ACOUSTIC		94  /* acoustic mgmt values ( >= ATA-6) */
#define LBA_LSB			100 /* LBA: maximum.  Currently only 48 */
#define LBA_MID			101 /*      bits are used, but addr 103 */
#define LBA_48_MSB		102 /*      has been reserved for LBA in */
#define LBA_64_MSB		103 /*      the future. */
#define RM_STAT			127 /* removable media status notification feature set support */
#define SECU_STATUS		128 /* security status */
#define CFA_PWR_MODE		160 /* CFA power mode 1 */
#define START_MEDIA             176 /* media serial number */
#define LENGTH_MEDIA            20  /* 20 words (40 bytes or characters)*/
#define START_MANUF             196 /* media manufacturer I.D. */
#define LENGTH_MANUF            10  /* 10 words (20 bytes or characters) */
#define INTEGRITY		255 /* integrity word */

/* bit definitions within the words */
/* -------------------------------- */

/* many words are considered valid if bit 15 is 0 and bit 14 is 1 */
#define VALID			0xc000
#define VALID_VAL		0x4000
/* many words are considered invalid if they are either all-0 or all-1 */
#define NOVAL_0			0x0000
#define NOVAL_1			0xffff

/* word 0: gen_config */
#define NOT_ATA			0x8000
#define NOT_ATAPI		0x4000	/* (check only if bit 15 == 1) */
#define MEDIA_REMOVABLE		0x0080
#define DRIVE_NOT_REMOVABLE	0x0040  /* bit obsoleted in ATA 6 */
#define INCOMPLETE		0x0004
#define CFA_SUPPORT_VAL		0x848a	/* 848a=CFA feature set support */
#define DRQ_RESPONSE_TIME	0x0060
#define DRQ_3MS_VAL		0x0000
#define DRQ_INTR_VAL		0x0020
#define DRQ_50US_VAL		0x0040
#define PKT_SIZE_SUPPORTED	0x0003
#define PKT_SIZE_12_VAL		0x0000
#define PKT_SIZE_16_VAL		0x0001
#define EQPT_TYPE		0x1f00
#define SHIFT_EQPT		8

#define CDROM 0x0005

/* word 1: number of logical cylinders */
#define LCYLS_MAX		0x3fff /* maximum allowable value */

/* word 2: specific configuration
 * (a) require SET FEATURES to spin-up
 * (b) require spin-up to fully reply to IDENTIFY DEVICE
 */
#define STBY_NID_VAL		0x37c8  /*     (a) and     (b) */
#define STBY_ID_VAL		0x738c	/*     (a) and not (b) */
#define PWRD_NID_VAL		0x8c73	/* not (a) and     (b) */
#define PWRD_ID_VAL		0xc837	/* not (a) and not (b) */

/* words 47 & 59: sector_xfer_max & sector_xfer_cur */
#define SECTOR_XFER		0x00ff  /* sectors xfered on r/w multiple cmds*/
#define MULTIPLE_SETTING_VALID  0x0100  /* 1=multiple sector setting is valid */

/* word 49: capabilities 0 */
#define STD_STBY		0x2000  /* 1=standard values supported (ATA); 0=vendor specific values */
#define IORDY_SUP		0x0800  /* 1=support; 0=may be supported */
#define IORDY_OFF		0x0400  /* 1=may be disabled */
#define LBA_SUP			0x0200  /* 1=Logical Block Address support */
#define DMA_SUP			0x0100  /* 1=Direct Memory Access support */
#define DMA_IL_SUP		0x8000  /* 1=interleaved DMA support (ATAPI) */
#define CMD_Q_SUP		0x4000  /* 1=command queuing support (ATAPI) */
#define OVLP_SUP		0x2000  /* 1=overlap operation support (ATAPI) */
#define SWRST_REQ		0x1000  /* 1=ATA SW reset required (ATAPI, obsolete */

/* word 50: capabilities 1 */
#define MIN_STANDBY_TIMER	0x0001  /* 1=device specific standby timer value minimum */

/* words 51 & 52: PIO & DMA cycle times */
#define MODE			0xff00  /* the mode is in the MSBs */

/* word 53: whats_valid */
#define OK_W88			0x0004	/* the ultra_dma info is valid */
#define OK_W64_70		0x0002  /* see above for word descriptions */
#define OK_W54_58		0x0001  /* current cyl, head, sector, cap. info valid */

/*word 63,88: dma_mode, ultra_dma_mode*/
#define MODE_MAX		7	/* bit definitions force udma <=7 (when
					 * udma >=8 comes out it'll have to be
					 * defined in a new dma_mode word!) */

/* word 64: PIO transfer modes */
#define PIO_SUP			0x00ff  /* only bits 0 & 1 are used so far,  */
#define PIO_MODE_MAX		8       /* but all 8 bits are defined        */

/* word 75: queue_depth */
#define DEPTH_BITS		0x001f  /* bits used for queue depth */

/* words 80-81: version numbers */
/* NOVAL_0 or  NOVAL_1 means device does not report version */

/* word 81: minor version number */
#define MINOR_MAX		0x22
/* words 82-84: cmds/feats supported */
#define CMDS_W82		0x77ff  /* word 82: defined command locations*/
#define CMDS_W83		0x3fff  /* word 83: defined command locations*/
#define CMDS_W84		0x002f  /* word 83: defined command locations*/
#define SUPPORT_48_BIT		0x0400
#define NUM_CMD_FEAT_STR	48

/* words 85-87: cmds/feats enabled */
/* use cmd_feat_str[] to display what commands and features have
 * been enabled with words 85-87
 */

/* words 89, 90, SECU ERASE TIME */
#define ERASE_BITS      0x00ff

/* word 92: master password revision */
/* NOVAL_0 or  NOVAL_1 means no support for master password revision */

/* word 93: hw reset result */
#define CBLID           0x2000  /* CBLID status */
#define RST0            0x0001  /* 1=reset to device #0 */
#define DEV_DET         0x0006  /* how device num determined */
#define JUMPER_VAL      0x0002  /* device num determined by jumper */
#define CSEL_VAL        0x0004  /* device num determined by CSEL_VAL */

/* word 127: removable media status notification feature set support */
#define RM_STAT_BITS    0x0003
#define RM_STAT_SUP     0x0001

/* word 128: security */
#define SECU_ENABLED    0x0002
#define SECU_LEVEL      0x0010
#define NUM_SECU_STR    6

/* word 160: CFA power mode */
#define VALID_W160              0x8000  /* 1=word valid */
#define PWR_MODE_REQ            0x2000  /* 1=CFA power mode req'd by some cmds*/
#define PWR_MODE_OFF            0x1000  /* 1=CFA power moded disabled */
#define MAX_AMPS                0x0fff  /* value = max current in ma */

/* word 255: integrity */
#define SIG                     0x00ff  /* signature location */
#define SIG_VAL                 0x00a5  /* signature value */

#define TIMING_BUF_MB           1
#define TIMING_BUF_BYTES        (TIMING_BUF_MB * 1024 * 1024)

#undef DO_FLUSHCACHE            /* under construction: force cache flush on -W0 */


#define IS_GET 1
#define IS_SET 2


enum { fd = 3 };


struct globals {
	smallint get_identity, get_geom;
	smallint do_flush;
	smallint do_ctimings, do_timings;
	smallint reread_partn;
	smallint set_piomode, noisy_piomode;
	smallint getset_readahead;
	smallint getset_readonly;
	smallint getset_unmask;
	smallint getset_mult;
#ifdef HDIO_GET_QDMA
	smallint getset_dma_q;
#endif
	smallint getset_nowerr;
	smallint getset_keep;
	smallint getset_io32bit;
	int piomode;
	unsigned long Xreadahead;
	unsigned long readonly;
	unsigned long unmask;
	unsigned long mult;
#ifdef HDIO_SET_QDMA
	unsigned long dma_q;
#endif
	unsigned long nowerr;
	unsigned long keep;
	unsigned long io32bit;
#if ENABLE_FEATURE_HDPARM_HDIO_GETSET_DMA
	unsigned long dma;
	smallint getset_dma;
#endif
#ifdef HDIO_DRIVE_CMD
	smallint set_xfermode, get_xfermode;
	smallint getset_dkeep;
	smallint getset_standby;
	smallint getset_lookahead;
	smallint getset_prefetch;
	smallint getset_defects;
	smallint getset_wcache;
	smallint getset_doorlock;
	smallint set_seagate;
	smallint set_standbynow;
	smallint set_sleepnow;
	smallint get_powermode;
	smallint getset_apmmode;
	int xfermode_requested;
	unsigned long dkeep;
	unsigned long standby_requested; /* 0..255 */
	unsigned long lookahead;
	unsigned long prefetch;
	unsigned long defects;
	unsigned long wcache;
	unsigned long doorlock;
	unsigned long apmmode;
#endif
	IF_FEATURE_HDPARM_GET_IDENTITY(        smallint get_IDentity;)
	IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF(  smallint getset_busstate;)
	IF_FEATURE_HDPARM_HDIO_DRIVE_RESET(    smallint perform_reset;)
	IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF(  smallint perform_tristate;)
	IF_FEATURE_HDPARM_HDIO_UNREGISTER_HWIF(smallint unregister_hwif;)
	IF_FEATURE_HDPARM_HDIO_SCAN_HWIF(      smallint scan_hwif;)
	IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF(  unsigned long busstate;)
	IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF(  unsigned long tristate;)
	IF_FEATURE_HDPARM_HDIO_UNREGISTER_HWIF(unsigned long hwif;)
#if ENABLE_FEATURE_HDPARM_HDIO_SCAN_HWIF
	unsigned long hwif_data;
	unsigned long hwif_ctrl;
	unsigned long hwif_irq;
#endif
#ifdef DO_FLUSHCACHE
	unsigned char flushcache[4] = { WIN_FLUSHCACHE, 0, 0, 0 };
#endif
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define get_identity       (G.get_identity           )
#define get_geom           (G.get_geom               )
#define do_flush           (G.do_flush               )
#define do_ctimings        (G.do_ctimings            )
#define do_timings         (G.do_timings             )
#define reread_partn       (G.reread_partn           )
#define set_piomode        (G.set_piomode            )
#define noisy_piomode      (G.noisy_piomode          )
#define getset_readahead   (G.getset_readahead       )
#define getset_readonly    (G.getset_readonly        )
#define getset_unmask      (G.getset_unmask          )
#define getset_mult        (G.getset_mult            )
#define getset_dma_q       (G.getset_dma_q           )
#define getset_nowerr      (G.getset_nowerr          )
#define getset_keep        (G.getset_keep            )
#define getset_io32bit     (G.getset_io32bit         )
#define piomode            (G.piomode                )
#define Xreadahead         (G.Xreadahead             )
#define readonly           (G.readonly               )
#define unmask             (G.unmask                 )
#define mult               (G.mult                   )
#define dma_q              (G.dma_q                  )
#define nowerr             (G.nowerr                 )
#define keep               (G.keep                   )
#define io32bit            (G.io32bit                )
#define dma                (G.dma                    )
#define getset_dma         (G.getset_dma             )
#define set_xfermode       (G.set_xfermode           )
#define get_xfermode       (G.get_xfermode           )
#define getset_dkeep       (G.getset_dkeep           )
#define getset_standby     (G.getset_standby         )
#define getset_lookahead   (G.getset_lookahead       )
#define getset_prefetch    (G.getset_prefetch        )
#define getset_defects     (G.getset_defects         )
#define getset_wcache      (G.getset_wcache          )
#define getset_doorlock    (G.getset_doorlock        )
#define set_seagate        (G.set_seagate            )
#define set_standbynow     (G.set_standbynow         )
#define set_sleepnow       (G.set_sleepnow           )
#define get_powermode      (G.get_powermode          )
#define getset_apmmode     (G.getset_apmmode         )
#define xfermode_requested (G.xfermode_requested     )
#define dkeep              (G.dkeep                  )
#define standby_requested  (G.standby_requested      )
#define lookahead          (G.lookahead              )
#define prefetch           (G.prefetch               )
#define defects            (G.defects                )
#define wcache             (G.wcache                 )
#define doorlock           (G.doorlock               )
#define apmmode            (G.apmmode                )
#define get_IDentity       (G.get_IDentity           )
#define getset_busstate    (G.getset_busstate        )
#define perform_reset      (G.perform_reset          )
#define perform_tristate   (G.perform_tristate       )
#define unregister_hwif    (G.unregister_hwif        )
#define scan_hwif          (G.scan_hwif              )
#define busstate           (G.busstate               )
#define tristate           (G.tristate               )
#define hwif               (G.hwif                   )
#define hwif_data          (G.hwif_data              )
#define hwif_ctrl          (G.hwif_ctrl              )
#define hwif_irq           (G.hwif_irq               )
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
} while (0)


/* Busybox messages and functions */
#if ENABLE_IOCTL_HEX2STR_ERROR
static int ioctl_alt_func(/*int fd,*/ int cmd, unsigned char *args, int alt, const char *string)
{
	if (!ioctl(fd, cmd, args))
		return 0;
	args[0] = alt;
	return bb_ioctl_or_warn(fd, cmd, args, string);
}
#define ioctl_alt_or_warn(cmd,args,alt) ioctl_alt_func(cmd,args,alt,#cmd)
#else
static int ioctl_alt_func(/*int fd,*/ int cmd, unsigned char *args, int alt)
{
	if (!ioctl(fd, cmd, args))
		return 0;
	args[0] = alt;
	return bb_ioctl_or_warn(fd, cmd, args);
}
#define ioctl_alt_or_warn(cmd,args,alt) ioctl_alt_func(cmd,args,alt)
#endif

static void on_off(int value)
{
	puts(value ? " (on)" : " (off)");
}

static void print_flag_on_off(int get_arg, const char *s, unsigned long arg)
{
	if (get_arg) {
		printf(" setting %s to %lu", s, arg);
		on_off(arg);
	}
}

static void print_value_on_off(const char *str, unsigned long argp)
{
	printf(" %s\t= %2lu", str, argp);
	on_off(argp != 0);
}

#if ENABLE_FEATURE_HDPARM_GET_IDENTITY
static void print_ascii(const char *p, int length)
{
#if BB_BIG_ENDIAN
#define LE_ONLY(x)
	enum { ofs = 0 };
#else
#define LE_ONLY(x) x
	/* every 16bit word is big-endian (i.e. inverted) */
	/* accessing bytes in 1,0, 3,2, 5,4... sequence */
	int ofs = 1;
#endif

	length *= 2;
	/* find first non-space & print it */
	while (length && p[ofs] != ' ') {
		p++;
		LE_ONLY(ofs = -ofs;)
		length--;
	}
	while (length && p[ofs]) {
		bb_putchar(p[ofs]);
		p++;
		LE_ONLY(ofs = -ofs;)
		length--;
	}
	bb_putchar('\n');
#undef LE_ONLY
}

static void xprint_ascii(uint16_t *val, int i, const char *string, int n)
{
	if (val[i]) {
		printf("\t%-20s", string);
		print_ascii((void*)&val[i], n);
	}
}

static uint8_t mode_loop(uint16_t mode_sup, uint16_t mode_sel, int cc, uint8_t *have_mode)
{
	uint16_t ii;
	uint8_t err_dma = 0;

	for (ii = 0; ii <= MODE_MAX; ii++) {
		if (mode_sel & 0x0001) {
			printf("*%cdma%u ", cc, ii);
			if (*have_mode)
				err_dma = 1;
			*have_mode = 1;
		} else if (mode_sup & 0x0001)
			printf("%cdma%u ", cc, ii);

		mode_sup >>= 1;
		mode_sel >>= 1;
	}
	return err_dma;
}

static const char pkt_str[] ALIGN1 =
	"Direct-access device" "\0"             /* word 0, bits 12-8 = 00 */
	"Sequential-access device" "\0"         /* word 0, bits 12-8 = 01 */
	"Printer" "\0"                          /* word 0, bits 12-8 = 02 */
	"Processor" "\0"                        /* word 0, bits 12-8 = 03 */
	"Write-once device" "\0"                /* word 0, bits 12-8 = 04 */
	"CD-ROM" "\0"                           /* word 0, bits 12-8 = 05 */
	"Scanner" "\0"                          /* word 0, bits 12-8 = 06 */
	"Optical memory" "\0"                   /* word 0, bits 12-8 = 07 */
	"Medium changer" "\0"                   /* word 0, bits 12-8 = 08 */
	"Communications device" "\0"            /* word 0, bits 12-8 = 09 */
	"ACS-IT8 device" "\0"                   /* word 0, bits 12-8 = 0a */
	"ACS-IT8 device" "\0"                   /* word 0, bits 12-8 = 0b */
	"Array controller" "\0"                 /* word 0, bits 12-8 = 0c */
	"Enclosure services" "\0"               /* word 0, bits 12-8 = 0d */
	"Reduced block command device" "\0"     /* word 0, bits 12-8 = 0e */
	"Optical card reader/writer" "\0"       /* word 0, bits 12-8 = 0f */
;

static const char ata1_cfg_str[] ALIGN1 =       /* word 0 in ATA-1 mode */
	"reserved" "\0"                         /* bit 0 */
	"hard sectored" "\0"                    /* bit 1 */
	"soft sectored" "\0"                    /* bit 2 */
	"not MFM encoded " "\0"                 /* bit 3 */
	"head switch time > 15us" "\0"          /* bit 4 */
	"spindle motor control option" "\0"     /* bit 5 */
	"fixed drive" "\0"                      /* bit 6 */
	"removable drive" "\0"                  /* bit 7 */
	"disk xfer rate <= 5Mbs" "\0"           /* bit 8 */
	"disk xfer rate > 5Mbs, <= 10Mbs" "\0"  /* bit 9 */
	"disk xfer rate > 5Mbs" "\0"            /* bit 10 */
	"rotational speed tol." "\0"            /* bit 11 */
	"data strobe offset option" "\0"        /* bit 12 */
	"track offset option" "\0"              /* bit 13 */
	"format speed tolerance gap reqd" "\0"  /* bit 14 */
	"ATAPI"                                 /* bit 14 */
;

static const char minor_str[] ALIGN1 =
	/* word 81 value: */
	"Unspecified" "\0"                                  /* 0x0000 */
	"ATA-1 X3T9.2 781D prior to rev.4" "\0"             /* 0x0001 */
	"ATA-1 published, ANSI X3.221-1994" "\0"            /* 0x0002 */
	"ATA-1 X3T9.2 781D rev.4" "\0"                      /* 0x0003 */
	"ATA-2 published, ANSI X3.279-1996" "\0"            /* 0x0004 */
	"ATA-2 X3T10 948D prior to rev.2k" "\0"             /* 0x0005 */
	"ATA-3 X3T10 2008D rev.1" "\0"                      /* 0x0006 */
	"ATA-2 X3T10 948D rev.2k" "\0"                      /* 0x0007 */
	"ATA-3 X3T10 2008D rev.0" "\0"                      /* 0x0008 */
	"ATA-2 X3T10 948D rev.3" "\0"                       /* 0x0009 */
	"ATA-3 published, ANSI X3.298-199x" "\0"            /* 0x000a */
	"ATA-3 X3T10 2008D rev.6" "\0"                      /* 0x000b */
	"ATA-3 X3T13 2008D rev.7 and 7a" "\0"               /* 0x000c */
	"ATA/ATAPI-4 X3T13 1153D rev.6" "\0"                /* 0x000d */
	"ATA/ATAPI-4 T13 1153D rev.13" "\0"                 /* 0x000e */
	"ATA/ATAPI-4 X3T13 1153D rev.7" "\0"                /* 0x000f */
	"ATA/ATAPI-4 T13 1153D rev.18" "\0"                 /* 0x0010 */
	"ATA/ATAPI-4 T13 1153D rev.15" "\0"                 /* 0x0011 */
	"ATA/ATAPI-4 published, ANSI INCITS 317-1998" "\0"  /* 0x0012 */
	"ATA/ATAPI-5 T13 1321D rev.3" "\0"                  /* 0x0013 */
	"ATA/ATAPI-4 T13 1153D rev.14" "\0"                 /* 0x0014 */
	"ATA/ATAPI-5 T13 1321D rev.1" "\0"                  /* 0x0015 */
	"ATA/ATAPI-5 published, ANSI INCITS 340-2000" "\0"  /* 0x0016 */
	"ATA/ATAPI-4 T13 1153D rev.17" "\0"                 /* 0x0017 */
	"ATA/ATAPI-6 T13 1410D rev.0" "\0"                  /* 0x0018 */
	"ATA/ATAPI-6 T13 1410D rev.3a" "\0"                 /* 0x0019 */
	"ATA/ATAPI-7 T13 1532D rev.1" "\0"                  /* 0x001a */
	"ATA/ATAPI-6 T13 1410D rev.2" "\0"                  /* 0x001b */
	"ATA/ATAPI-6 T13 1410D rev.1" "\0"                  /* 0x001c */
	"ATA/ATAPI-7 published, ANSI INCITS 397-2005" "\0"  /* 0x001d */
	"ATA/ATAPI-7 T13 1532D rev.0" "\0"                  /* 0x001e */
	"reserved" "\0"                                     /* 0x001f */
	"reserved" "\0"                                     /* 0x0020 */
	"ATA/ATAPI-7 T13 1532D rev.4a" "\0"                 /* 0x0021 */
	"ATA/ATAPI-6 published, ANSI INCITS 361-2002" "\0"  /* 0x0022 */
	"reserved"                                          /* 0x0023-0xfffe */
;
static const char actual_ver[MINOR_MAX + 2] ALIGN1 = {
	   /* word 81 value: */
	0, /* 0x0000 WARNING: actual_ver[] array */
	1, /* 0x0001 WARNING: corresponds        */
	1, /* 0x0002 WARNING: *exactly*          */
	1, /* 0x0003 WARNING: to the ATA/        */
	2, /* 0x0004 WARNING: ATAPI version      */
	2, /* 0x0005 WARNING: listed in          */
	3, /* 0x0006 WARNING: the                */
	2, /* 0x0007 WARNING: minor_str          */
	3, /* 0x0008 WARNING: array              */
	2, /* 0x0009 WARNING: above.             */
	3, /* 0x000a WARNING:                    */
	3, /* 0x000b WARNING: If you change      */
	3, /* 0x000c WARNING: that one,          */
	4, /* 0x000d WARNING: change this one    */
	4, /* 0x000e WARNING: too!!!             */
	4, /* 0x000f */
	4, /* 0x0010 */
	4, /* 0x0011 */
	4, /* 0x0012 */
	5, /* 0x0013 */
	4, /* 0x0014 */
	5, /* 0x0015 */
	5, /* 0x0016 */
	4, /* 0x0017 */
	6, /* 0x0018 */
	6, /* 0x0019 */
	7, /* 0x001a */
	6, /* 0x001b */
	6, /* 0x001c */
	7, /* 0x001d */
	7, /* 0x001e */
	0, /* 0x001f */
	0, /* 0x0020 */
	7, /* 0x0021 */
	6, /* 0x0022 */
	0  /* 0x0023-0xfffe */
};

static const char cmd_feat_str[] ALIGN1 =
	"" "\0"                                     /* word 82 bit 15: obsolete  */
	"NOP cmd" "\0"                              /* word 82 bit 14 */
	"READ BUFFER cmd" "\0"                      /* word 82 bit 13 */
	"WRITE BUFFER cmd" "\0"                     /* word 82 bit 12 */
	"" "\0"                                     /* word 82 bit 11: obsolete  */
	"Host Protected Area feature set" "\0"      /* word 82 bit 10 */
	"DEVICE RESET cmd" "\0"                     /* word 82 bit  9 */
	"SERVICE interrupt" "\0"                    /* word 82 bit  8 */
	"Release interrupt" "\0"                    /* word 82 bit  7 */
	"Look-ahead" "\0"                           /* word 82 bit  6 */
	"Write cache" "\0"                          /* word 82 bit  5 */
	"PACKET command feature set" "\0"           /* word 82 bit  4 */
	"Power Management feature set" "\0"         /* word 82 bit  3 */
	"Removable Media feature set" "\0"          /* word 82 bit  2 */
	"Security Mode feature set" "\0"            /* word 82 bit  1 */
	"SMART feature set" "\0"                    /* word 82 bit  0 */
	                                            /* -------------- */
	"" "\0"                                     /* word 83 bit 15: !valid bit */
	"" "\0"                                     /* word 83 bit 14:  valid bit */
	"FLUSH CACHE EXT cmd" "\0"                  /* word 83 bit 13 */
	"Mandatory FLUSH CACHE cmd " "\0"           /* word 83 bit 12 */
	"Device Configuration Overlay feature set " "\0"
	"48-bit Address feature set " "\0"          /* word 83 bit 10 */
	"" "\0"
	"SET MAX security extension" "\0"           /* word 83 bit  8 */
	"Address Offset Reserved Area Boot" "\0"    /* word 83 bit  7 */
	"SET FEATURES subcommand required to spinup after power up" "\0"
	"Power-Up In Standby feature set" "\0"      /* word 83 bit  5 */
	"Removable Media Status Notification feature set" "\0"
	"Adv. Power Management feature set" "\0"    /* word 83 bit  3 */
	"CFA feature set" "\0"                      /* word 83 bit  2 */
	"READ/WRITE DMA QUEUED" "\0"                /* word 83 bit  1 */
	"DOWNLOAD MICROCODE cmd" "\0"               /* word 83 bit  0 */
	                                            /* -------------- */
	"" "\0"                                     /* word 84 bit 15: !valid bit */
	"" "\0"                                     /* word 84 bit 14:  valid bit */
	"" "\0"                                     /* word 84 bit 13:  reserved */
	"" "\0"                                     /* word 84 bit 12:  reserved */
	"" "\0"                                     /* word 84 bit 11:  reserved */
	"" "\0"                                     /* word 84 bit 10:  reserved */
	"" "\0"                                     /* word 84 bit  9:  reserved */
	"" "\0"                                     /* word 84 bit  8:  reserved */
	"" "\0"                                     /* word 84 bit  7:  reserved */
	"" "\0"                                     /* word 84 bit  6:  reserved */
	"General Purpose Logging feature set" "\0"  /* word 84 bit  5 */
	"" "\0"                                     /* word 84 bit  4:  reserved */
	"Media Card Pass Through Command feature set " "\0"
	"Media serial number " "\0"                 /* word 84 bit  2 */
	"SMART self-test " "\0"                     /* word 84 bit  1 */
	"SMART error logging "                      /* word 84 bit  0 */
;

static const char secu_str[] ALIGN1 =
	"supported" "\0"                /* word 128, bit 0 */
	"enabled" "\0"                  /* word 128, bit 1 */
	"locked" "\0"                   /* word 128, bit 2 */
	"frozen" "\0"                   /* word 128, bit 3 */
	"expired: security count" "\0"  /* word 128, bit 4 */
	"supported: enhanced erase"     /* word 128, bit 5 */
;

// Parse 512 byte disk identification block and print much crap.
static void identify(uint16_t *val) NORETURN;
static void identify(uint16_t *val)
{
	uint16_t ii, jj, kk;
	uint16_t like_std = 1, std = 0, min_std = 0xffff;
	uint16_t dev = NO_DEV, eqpt = NO_DEV;
	uint8_t  have_mode = 0, err_dma = 0;
	uint8_t  chksum = 0;
	uint32_t ll, mm, nn, oo;
	uint64_t bbbig; /* (:) */
	const char *strng;
#if BB_BIG_ENDIAN
	uint16_t buf[256];

	// Adjust for endianness
	swab(val, buf, sizeof(buf));
	val = buf;
#endif
	/* check if we recognize the device type */
	bb_putchar('\n');
	if (!(val[GEN_CONFIG] & NOT_ATA)) {
		dev = ATA_DEV;
		printf("ATA device, with ");
	} else if (val[GEN_CONFIG]==CFA_SUPPORT_VAL) {
		dev = ATA_DEV;
		like_std = 4;
		printf("CompactFlash ATA device, with ");
	} else if (!(val[GEN_CONFIG] & NOT_ATAPI)) {
		dev = ATAPI_DEV;
		eqpt = (val[GEN_CONFIG] & EQPT_TYPE) >> SHIFT_EQPT;
		printf("ATAPI %s, with ", eqpt <= 0xf ? nth_string(pkt_str, eqpt) : "unknown");
		like_std = 3;
	} else
		/* "Unknown device type:\n\tbits 15&14 of general configuration word 0 both set to 1.\n" */
		bb_error_msg_and_die("unknown device type");

	printf("%sremovable media\n", !(val[GEN_CONFIG] & MEDIA_REMOVABLE) ? "non-" : "");
	/* Info from the specific configuration word says whether or not the
	 * ID command completed correctly.  It is only defined, however in
	 * ATA/ATAPI-5 & 6; it is reserved (value theoretically 0) in prior
	 * standards.  Since the values allowed for this word are extremely
	 * specific, it should be safe to check it now, even though we don't
	 * know yet what standard this device is using.
	 */
	if ((val[CONFIG]==STBY_NID_VAL) || (val[CONFIG]==STBY_ID_VAL)
	 || (val[CONFIG]==PWRD_NID_VAL) || (val[CONFIG]==PWRD_ID_VAL)
	) {
		like_std = 5;
		if ((val[CONFIG]==STBY_NID_VAL) || (val[CONFIG]==STBY_ID_VAL))
			puts("powers-up in standby; SET FEATURES subcmd spins-up.");
		if (((val[CONFIG]==STBY_NID_VAL) || (val[CONFIG]==PWRD_NID_VAL)) && (val[GEN_CONFIG] & INCOMPLETE))
			puts("\n\tWARNING: ID response incomplete.\n\tFollowing data may be incorrect.\n");
	}

	/* output the model and serial numbers and the fw revision */
	xprint_ascii(val, START_MODEL,  "Model Number:",        LENGTH_MODEL);
	xprint_ascii(val, START_SERIAL, "Serial Number:",       LENGTH_SERIAL);
	xprint_ascii(val, START_FW_REV, "Firmware Revision:",   LENGTH_FW_REV);
	xprint_ascii(val, START_MEDIA,  "Media Serial Num:",    LENGTH_MEDIA);
	xprint_ascii(val, START_MANUF,  "Media Manufacturer:",  LENGTH_MANUF);

	/* major & minor standards version number (Note: these words were not
	 * defined until ATA-3 & the CDROM std uses different words.) */
	printf("Standards:");
	if (eqpt != CDROM) {
		if (val[MINOR] && (val[MINOR] <= MINOR_MAX)) {
			if (like_std < 3) like_std = 3;
			std = actual_ver[val[MINOR]];
			if (std)
				printf("\n\tUsed: %s ", nth_string(minor_str, val[MINOR]));
		}
		/* looks like when they up-issue the std, they obsolete one;
		 * thus, only the newest 4 issues need be supported. (That's
		 * what "kk" and "min_std" are all about.) */
		if (val[MAJOR] && (val[MAJOR] != NOVAL_1)) {
			printf("\n\tSupported: ");
			jj = val[MAJOR] << 1;
			kk = like_std >4 ? like_std-4: 0;
			for (ii = 14; (ii >0)&&(ii>kk); ii--) {
				if (jj & 0x8000) {
					printf("%u ", ii);
					if (like_std < ii) {
						like_std = ii;
						kk = like_std >4 ? like_std-4: 0;
					}
					if (min_std > ii) min_std = ii;
				}
				jj <<= 1;
			}
			if (like_std < 3) like_std = 3;
		}
		/* Figure out what standard the device is using if it hasn't told
		 * us.  If we know the std, check if the device is using any of
		 * the words from the next level up.  It happens.
		 */
		if (like_std < std) like_std = std;

		if (((std == 5) || (!std && (like_std < 6))) &&
			((((val[CMDS_SUPP_1] & VALID) == VALID_VAL) &&
			((	val[CMDS_SUPP_1] & CMDS_W83) > 0x00ff)) ||
			(((	val[CMDS_SUPP_2] & VALID) == VALID_VAL) &&
			(	val[CMDS_SUPP_2] & CMDS_W84) ) )
		) {
			like_std = 6;
		} else if (((std == 4) || (!std && (like_std < 5))) &&
			((((val[INTEGRITY]	& SIG) == SIG_VAL) && !chksum) ||
			((	val[HWRST_RSLT] & VALID) == VALID_VAL) ||
			(((	val[CMDS_SUPP_1] & VALID) == VALID_VAL) &&
			((	val[CMDS_SUPP_1] & CMDS_W83) > 0x001f)) ) )
		{
			like_std = 5;
		} else if (((std == 3) || (!std && (like_std < 4))) &&
				((((val[CMDS_SUPP_1] & VALID) == VALID_VAL) &&
				(((	val[CMDS_SUPP_1] & CMDS_W83) > 0x0000) ||
				((	val[CMDS_SUPP_0] & CMDS_W82) > 0x000f))) ||
				((	val[CAPAB_1] & VALID) == VALID_VAL) ||
				((	val[WHATS_VALID] & OK_W88) && val[ULTRA_DMA]) ||
				((	val[RM_STAT] & RM_STAT_BITS) == RM_STAT_SUP) )
		) {
			like_std = 4;
		} else if (((std == 2) || (!std && (like_std < 3)))
		 && ((val[CMDS_SUPP_1] & VALID) == VALID_VAL)
		) {
			like_std = 3;
		} else if (((std == 1) || (!std && (like_std < 2))) &&
				((val[CAPAB_0] & (IORDY_SUP | IORDY_OFF)) ||
				(val[WHATS_VALID] & OK_W64_70)) )
		{
			like_std = 2;
		}

		if (!std)
			printf("\n\tLikely used: %u\n", like_std);
		else if (like_std > std)
			printf("& some of %u\n", like_std);
		else
			bb_putchar('\n');
	} else {
		/* TBD: do CDROM stuff more thoroughly.  For now... */
		kk = 0;
		if (val[CDR_MINOR] == 9) {
			kk = 1;
			printf("\n\tUsed: ATAPI for CD-ROMs, SFF-8020i, r2.5");
		}
		if (val[CDR_MAJOR] && (val[CDR_MAJOR] !=NOVAL_1)) {
			kk = 1;
			printf("\n\tSupported: CD-ROM ATAPI");
			jj = val[CDR_MAJOR] >> 1;
			for (ii = 1; ii < 15; ii++) {
				if (jj & 0x0001) printf("-%u ", ii);
				jj >>= 1;
			}
		}
		puts(kk ? "" : "\n\tLikely used CD-ROM ATAPI-1");
		/* the cdrom stuff is more like ATA-2 than anything else, so: */
		like_std = 2;
	}

	if (min_std == 0xffff)
		min_std = like_std > 4 ? like_std - 3 : 1;

	puts("Configuration:");
	/* more info from the general configuration word */
	if ((eqpt != CDROM) && (like_std == 1)) {
		jj = val[GEN_CONFIG] >> 1;
		for (ii = 1; ii < 15; ii++) {
			if (jj & 0x0001)
				printf("\t%s\n", nth_string(ata1_cfg_str, ii));
			jj >>=1;
		}
	}
	if (dev == ATAPI_DEV) {
		if ((val[GEN_CONFIG] & DRQ_RESPONSE_TIME) ==  DRQ_3MS_VAL)
			strng = "3ms";
		else if ((val[GEN_CONFIG] & DRQ_RESPONSE_TIME) ==  DRQ_INTR_VAL)
			strng = "<=10ms with INTRQ";
		else if ((val[GEN_CONFIG] & DRQ_RESPONSE_TIME) ==  DRQ_50US_VAL)
			strng ="50us";
		else
			strng = "unknown";
		printf("\tDRQ response: %s\n\tPacket size: ", strng); /* Data Request (DRQ) */

		if ((val[GEN_CONFIG] & PKT_SIZE_SUPPORTED) == PKT_SIZE_12_VAL)
			strng = "12 bytes";
		else if ((val[GEN_CONFIG] & PKT_SIZE_SUPPORTED) == PKT_SIZE_16_VAL)
			strng = "16 bytes";
		else
			strng = "unknown";
		puts(strng);
	} else {
		/* addressing...CHS? See section 6.2 of ATA specs 4 or 5 */
		ll = (uint32_t)val[LBA_SECTS_MSB] << 16 | val[LBA_SECTS_LSB];
		mm = 0;
		bbbig = 0;
		if ((ll > 0x00FBFC10) && (!val[LCYLS]))
			puts("\tCHS addressing not supported");
		else {
			jj = val[WHATS_VALID] & OK_W54_58;
			printf("\tLogical\t\tmax\tcurrent\n"
				"\tcylinders\t%u\t%u\n"
				"\theads\t\t%u\t%u\n"
				"\tsectors/track\t%u\t%u\n"
				"\t--\n",
				val[LCYLS],
				jj ? val[LCYLS_CUR] : 0,
				val[LHEADS],
				jj ? val[LHEADS_CUR] : 0,
				val[LSECTS],
				jj ? val[LSECTS_CUR] : 0);

			if ((min_std == 1) && (val[TRACK_BYTES] || val[SECT_BYTES]))
				printf("\tbytes/track: %u\tbytes/sector: %u\n",
					val[TRACK_BYTES], val[SECT_BYTES]);

			if (jj) {
				mm = (uint32_t)val[CAPACITY_MSB] << 16 | val[CAPACITY_LSB];
				if (like_std < 3) {
					/* check Endian of capacity bytes */
					nn = val[LCYLS_CUR] * val[LHEADS_CUR] * val[LSECTS_CUR];
					oo = (uint32_t)val[CAPACITY_LSB] << 16 | val[CAPACITY_MSB];
					if (abs(mm - nn) > abs(oo - nn))
						mm = oo;
				}
				printf("\tCHS current addressable sectors:%11u\n", mm);
			}
		}
		/* LBA addressing */
		printf("\tLBA    user addressable sectors:%11u\n", ll);
		if (((val[CMDS_SUPP_1] & VALID) == VALID_VAL)
		 && (val[CMDS_SUPP_1] & SUPPORT_48_BIT)
		) {
			bbbig = (uint64_t)val[LBA_64_MSB] << 48 |
			        (uint64_t)val[LBA_48_MSB] << 32 |
			        (uint64_t)val[LBA_MID] << 16 |
					val[LBA_LSB];
			printf("\tLBA48  user addressable sectors:%11"PRIu64"\n", bbbig);
		}

		if (!bbbig)
			bbbig = (uint64_t)(ll>mm ? ll : mm); /* # 512 byte blocks */
		printf("\tdevice size with M = 1024*1024: %11"PRIu64" MBytes\n", bbbig>>11);
		bbbig = (bbbig << 9) / 1000000;
		printf("\tdevice size with M = 1000*1000: %11"PRIu64" MBytes ", bbbig);

		if (bbbig > 1000)
			printf("(%"PRIu64" GB)\n", bbbig/1000);
		else
			bb_putchar('\n');
	}

	/* hw support of commands (capabilities) */
	printf("Capabilities:\n\t");

	if (dev == ATAPI_DEV) {
		if (eqpt != CDROM && (val[CAPAB_0] & CMD_Q_SUP))
			printf("Cmd queuing, ");
		if (val[CAPAB_0] & OVLP_SUP)
			printf("Cmd overlap, ");
	}
	if (val[CAPAB_0] & LBA_SUP) printf("LBA, ");

	if (like_std != 1) {
		printf("IORDY%s(can%s be disabled)\n",
			!(val[CAPAB_0] & IORDY_SUP) ? "(may be)" : "",
			(val[CAPAB_0] & IORDY_OFF) ? "" :"not");
	} else
		puts("no IORDY");

	if ((like_std == 1) && val[BUF_TYPE]) {
		printf("\tBuffer type: %04x: %s%s\n", val[BUF_TYPE],
			(val[BUF_TYPE] < 2) ? "single port, single-sector" : "dual port, multi-sector",
			(val[BUF_TYPE] > 2) ? " with read caching ability" : "");
	}

	if ((min_std == 1) && (val[BUFFER__SIZE] && (val[BUFFER__SIZE] != NOVAL_1))) {
		printf("\tBuffer size: %.1fkB\n", (float)val[BUFFER__SIZE]/2);
	}
	if ((min_std < 4) && (val[RW_LONG])) {
		printf("\tbytes avail on r/w long: %u\n", val[RW_LONG]);
	}
	if ((eqpt != CDROM) && (like_std > 3)) {
		printf("\tQueue depth: %u\n", (val[QUEUE_DEPTH] & DEPTH_BITS) + 1);
	}

	if (dev == ATA_DEV) {
		if (like_std == 1)
			printf("\tCan%s perform double-word IO\n", (!val[DWORD_IO]) ? "not" : "");
		else {
			printf("\tStandby timer values: spec'd by %s",
				(val[CAPAB_0] & STD_STBY) ? "standard" : "vendor");
			if ((like_std > 3) && ((val[CAPAB_1] & VALID) == VALID_VAL))
				printf(", %s device specific minimum\n",
					(val[CAPAB_1] & MIN_STANDBY_TIMER) ? "with" : "no");
			else
				bb_putchar('\n');
		}
		printf("\tR/W multiple sector transfer: ");
		if ((like_std < 3) && !(val[SECTOR_XFER_MAX] & SECTOR_XFER))
			puts("not supported");
		else {
			printf("Max = %u\tCurrent = ", val[SECTOR_XFER_MAX] & SECTOR_XFER);
			if (val[SECTOR_XFER_CUR] & MULTIPLE_SETTING_VALID)
				printf("%u\n", val[SECTOR_XFER_CUR] & SECTOR_XFER);
			else
				puts("?");
		}
		if ((like_std > 3) && (val[CMDS_SUPP_1] & 0x0008)) {
			/* We print out elsewhere whether the APM feature is enabled or
			 * not.  If it's not enabled, let's not repeat the info; just print
			 * nothing here. */
			printf("\tAdvancedPM level: ");
			if ((val[ADV_PWR] & 0xFF00) == 0x4000) {
				uint8_t apm_level = val[ADV_PWR] & 0x00FF;
				printf("%u (0x%x)\n", apm_level, apm_level);
			}
			else
				printf("unknown setting (0x%04x)\n", val[ADV_PWR]);
		}
		if (like_std > 5 && val[ACOUSTIC]) {
			printf("\tRecommended acoustic management value: %u, current value: %u\n",
				(val[ACOUSTIC] >> 8) & 0x00ff,
				val[ACOUSTIC] & 0x00ff);
		}
	} else {
		/* ATAPI */
		if (eqpt != CDROM && (val[CAPAB_0] & SWRST_REQ))
			puts("\tATA sw reset required");

		if (val[PKT_REL] || val[SVC_NBSY]) {
			printf("\tOverlap support:");
			if (val[PKT_REL])
				printf(" %uus to release bus.", val[PKT_REL]);
			if (val[SVC_NBSY])
				printf(" %uus to clear BSY after SERVICE cmd.",
					val[SVC_NBSY]);
			bb_putchar('\n');
		}
	}

	/* DMA stuff. Check that only one DMA mode is selected. */
	printf("\tDMA: ");
	if (!(val[CAPAB_0] & DMA_SUP))
		puts("not supported");
	else {
		if (val[DMA_MODE] && !val[SINGLE_DMA] && !val[MULTI_DMA])
			printf(" sdma%u\n", (val[DMA_MODE] & MODE) >> 8);
		if (val[SINGLE_DMA]) {
			jj = val[SINGLE_DMA];
			kk = val[SINGLE_DMA] >> 8;
			err_dma += mode_loop(jj, kk, 's', &have_mode);
		}
		if (val[MULTI_DMA]) {
			jj = val[MULTI_DMA];
			kk = val[MULTI_DMA] >> 8;
			err_dma += mode_loop(jj, kk, 'm', &have_mode);
		}
		if ((val[WHATS_VALID] & OK_W88) && val[ULTRA_DMA]) {
			jj = val[ULTRA_DMA];
			kk = val[ULTRA_DMA] >> 8;
			err_dma += mode_loop(jj, kk, 'u', &have_mode);
		}
		if (err_dma || !have_mode) printf("(?)");
		bb_putchar('\n');

		if ((dev == ATAPI_DEV) && (eqpt != CDROM) && (val[CAPAB_0] & DMA_IL_SUP))
			puts("\t\tInterleaved DMA support");

		if ((val[WHATS_VALID] & OK_W64_70)
		 && (val[DMA_TIME_MIN] || val[DMA_TIME_NORM])
		) {
			printf("\t\tCycle time:");
			if (val[DMA_TIME_MIN]) printf(" min=%uns", val[DMA_TIME_MIN]);
			if (val[DMA_TIME_NORM]) printf(" recommended=%uns", val[DMA_TIME_NORM]);
			bb_putchar('\n');
		}
	}

	/* Programmed IO stuff */
	printf("\tPIO: ");
	/* If a drive supports mode n (e.g. 3), it also supports all modes less
	 * than n (e.g. 3, 2, 1 and 0).  Print all the modes. */
	if ((val[WHATS_VALID] & OK_W64_70) && (val[ADV_PIO_MODES] & PIO_SUP)) {
		jj = ((val[ADV_PIO_MODES] & PIO_SUP) << 3) | 0x0007;
		for (ii = 0; ii <= PIO_MODE_MAX; ii++) {
			if (jj & 0x0001) printf("pio%d ", ii);
			jj >>=1;
		}
		bb_putchar('\n');
	} else if (((min_std < 5) || (eqpt == CDROM)) && (val[PIO_MODE] & MODE)) {
		for (ii = 0; ii <= val[PIO_MODE]>>8; ii++)
			printf("pio%d ", ii);
		bb_putchar('\n');
	} else
		puts("unknown");

	if (val[WHATS_VALID] & OK_W64_70) {
		if (val[PIO_NO_FLOW] || val[PIO_FLOW]) {
			printf("\t\tCycle time:");
			if (val[PIO_NO_FLOW])
				printf(" no flow control=%uns", val[PIO_NO_FLOW]);
			if (val[PIO_FLOW])
				printf("  IORDY flow control=%uns", val[PIO_FLOW]);
			bb_putchar('\n');
		}
	}

	if ((val[CMDS_SUPP_1] & VALID) == VALID_VAL) {
		puts("Commands/features:\n"
			"\tEnabled\tSupported:");
		jj = val[CMDS_SUPP_0];
		kk = val[CMDS_EN_0];
		for (ii = 0; ii < NUM_CMD_FEAT_STR; ii++) {
			const char *feat_str = nth_string(cmd_feat_str, ii);
			if ((jj & 0x8000) && (*feat_str != '\0')) {
				printf("\t%s\t%s\n", (kk & 0x8000) ? "   *" : "", feat_str);
			}
			jj <<= 1;
			kk <<= 1;
			if (ii % 16 == 15) {
				jj = val[CMDS_SUPP_0+1+(ii/16)];
				kk = val[CMDS_EN_0+1+(ii/16)];
			}
			if (ii == 31) {
				if ((val[CMDS_SUPP_2] & VALID) != VALID_VAL)
					ii +=16;
			}
		}
	}
	/* Removable Media Status Notification feature set */
	if ((val[RM_STAT] & RM_STAT_BITS) == RM_STAT_SUP)
		printf("\t%s supported\n", nth_string(cmd_feat_str, 27));

	/* security */
	if ((eqpt != CDROM) && (like_std > 3)
	 && (val[SECU_STATUS] || val[ERASE_TIME] || val[ENH_ERASE_TIME])
	) {
		puts("Security:");
		if (val[PSWD_CODE] && (val[PSWD_CODE] != NOVAL_1))
			printf("\tMaster password revision code = %u\n", val[PSWD_CODE]);
		jj = val[SECU_STATUS];
		if (jj) {
			for (ii = 0; ii < NUM_SECU_STR; ii++) {
				printf("\t%s\t%s\n",
					(!(jj & 0x0001)) ? "not" : "",
					nth_string(secu_str, ii));
				jj >>=1;
			}
			if (val[SECU_STATUS] & SECU_ENABLED) {
				printf("\tSecurity level %s\n",
					(val[SECU_STATUS] & SECU_LEVEL) ? "maximum" : "high");
			}
		}
		jj =  val[ERASE_TIME]     & ERASE_BITS;
		kk =  val[ENH_ERASE_TIME] & ERASE_BITS;
		if (jj || kk) {
			bb_putchar('\t');
			if (jj) printf("%umin for %sSECURITY ERASE UNIT. ", jj==ERASE_BITS ? 508 : jj<<1, "");
			if (kk) printf("%umin for %sSECURITY ERASE UNIT. ", kk==ERASE_BITS ? 508 : kk<<1, "ENHANCED ");
			bb_putchar('\n');
		}
	}

	/* reset result */
	jj = val[HWRST_RSLT];
	if ((jj & VALID) == VALID_VAL) {
		oo = (jj & RST0);
		if (!oo)
			jj >>= 8;
		if ((jj & DEV_DET) == JUMPER_VAL)
			strng = " determined by the jumper";
		else if ((jj & DEV_DET) == CSEL_VAL)
			strng = " determined by CSEL";
		else
			strng = "";
		printf("HW reset results:\n"
			"\tCBLID- %s Vih\n"
			"\tDevice num = %i%s\n",
			(val[HWRST_RSLT] & CBLID) ? "above" : "below",
			!(oo), strng);
	}

	/* more stuff from std 5 */
	if ((like_std > 4) && (eqpt != CDROM)) {
		if (val[CFA_PWR_MODE] & VALID_W160) {
			printf("CFA power mode 1:\n"
				"\t%s%s\n",
				(val[CFA_PWR_MODE] & PWR_MODE_OFF) ? "disabled" : "enabled",
				(val[CFA_PWR_MODE] & PWR_MODE_REQ) ? " and required by some commands" : "");
			if (val[CFA_PWR_MODE] & MAX_AMPS)
				printf("\tMaximum current = %uma\n", val[CFA_PWR_MODE] & MAX_AMPS);
		}
		if ((val[INTEGRITY] & SIG) == SIG_VAL) {
			printf("Checksum: %scorrect\n", chksum ? "in" : "");
		}
	}

	exit(EXIT_SUCCESS);
}
#endif

// Historically, if there was no HDIO_OBSOLETE_IDENTITY, then
// then the HDIO_GET_IDENTITY only returned 142 bytes.
// Otherwise, HDIO_OBSOLETE_IDENTITY returns 142 bytes,
// and HDIO_GET_IDENTITY returns 512 bytes.  But the latest
// 2.5.xx kernels no longer define HDIO_OBSOLETE_IDENTITY
// (which they should, but they should just return -EINVAL).
//
// So.. we must now assume that HDIO_GET_IDENTITY returns 512 bytes.
// On a really old system, it will not, and we will be confused.
// Too bad, really.

#if ENABLE_FEATURE_HDPARM_GET_IDENTITY
static const char cfg_str[] ALIGN1 =
	"""\0"            "HardSect""\0"   "SoftSect""\0"  "NotMFM""\0"
	"HdSw>15uSec""\0" "SpinMotCtl""\0" "Fixed""\0"     "Removeable""\0"
	"DTR<=5Mbs""\0"   "DTR>5Mbs""\0"   "DTR>10Mbs""\0" "RotSpdTol>.5%""\0"
	"dStbOff""\0"     "TrkOff""\0"     "FmtGapReq""\0" "nonMagnetic"
;

static const char BuffType[] ALIGN1 =
	"unknown""\0"     "1Sect""\0"      "DualPort""\0"  "DualPortCache"
;

static NOINLINE void dump_identity(const struct hd_driveid *id)
{
	int i;
	const unsigned short *id_regs = (const void*) id;

	printf("\n Model=%.40s, FwRev=%.8s, SerialNo=%.20s\n Config={",
				id->model, id->fw_rev, id->serial_no);
	for (i = 0; i <= 15; i++) {
		if (id->config & (1<<i))
			printf(" %s", nth_string(cfg_str, i));
	}
	printf(" }\n RawCHS=%u/%u/%u, TrkSize=%u, SectSize=%u, ECCbytes=%u\n"
		" BuffType=(%u) %s, BuffSize=%ukB, MaxMultSect=%u",
		id->cyls, id->heads, id->sectors, id->track_bytes,
		id->sector_bytes, id->ecc_bytes,
		id->buf_type,
		nth_string(BuffType, (id->buf_type > 3) ? 0 : id->buf_type),
		id->buf_size/2, id->max_multsect);
	if (id->max_multsect) {
		printf(", MultSect=");
		if (!(id->multsect_valid & 1))
			printf("?%u?", id->multsect);
		else if (id->multsect)
			printf("%u", id->multsect);
		else
			printf("off");
	}
	bb_putchar('\n');

	if (!(id->field_valid & 1))
		printf(" (maybe):");

	printf(" CurCHS=%u/%u/%u, CurSects=%lu, LBA=%s", id->cur_cyls, id->cur_heads,
		id->cur_sectors,
		(BB_BIG_ENDIAN) ?
			(unsigned long)(id->cur_capacity0 << 16) | id->cur_capacity1 :
			(unsigned long)(id->cur_capacity1 << 16) | id->cur_capacity0,
			((id->capability&2) == 0) ? "no" : "yes");

	if (id->capability & 2)
		printf(", LBAsects=%u", id->lba_capacity);

	printf("\n IORDY=%s",
		(id->capability & 8)
			? ((id->capability & 4) ? "on/off" : "yes")
			: "no");

	if (((id->capability & 8) || (id->field_valid & 2)) && (id->field_valid & 2))
		printf(", tPIO={min:%u,w/IORDY:%u}", id->eide_pio, id->eide_pio_iordy);

	if ((id->capability & 1) && (id->field_valid & 2))
		printf(", tDMA={min:%u,rec:%u}", id->eide_dma_min, id->eide_dma_time);

	printf("\n PIO modes:  ");
	if (id->tPIO <= 5) {
		printf("pio0 ");
		if (id->tPIO >= 1) printf("pio1 ");
		if (id->tPIO >= 2) printf("pio2 ");
	}
	if (id->field_valid & 2) {
		static const masks_labels_t pio_modes = {
			.masks = { 1, 2, ~3 },
			.labels = "pio3 \0""pio4 \0""pio? \0",
		};
		print_flags(&pio_modes, id->eide_pio_modes);
	}
	if (id->capability & 1) {
		if (id->dma_1word | id->dma_mword) {
			static const int dma_wmode_masks[] = { 0x100, 1, 0x200, 2, 0x400, 4, 0xf800, 0xf8 };
			printf("\n DMA modes:  ");
			print_flags_separated(dma_wmode_masks,
				"*\0""sdma0 \0""*\0""sdma1 \0""*\0""sdma2 \0""*\0""sdma? \0",
				id->dma_1word, NULL);
			print_flags_separated(dma_wmode_masks,
				"*\0""mdma0 \0""*\0""mdma1 \0""*\0""mdma2 \0""*\0""mdma? \0",
				id->dma_mword, NULL);
		}
	}
	if (((id->capability & 8) || (id->field_valid & 2)) && id->field_valid & 4) {
		static const masks_labels_t ultra_modes1 = {
			.masks = { 0x100, 0x001, 0x200, 0x002, 0x400, 0x004 },
			.labels = "*\0""udma0 \0""*\0""udma1 \0""*\0""udma2 \0",
		};

		printf("\n UDMA modes: ");
		print_flags(&ultra_modes1, id->dma_ultra);
#ifdef __NEW_HD_DRIVE_ID
		if (id->hw_config & 0x2000) {
#else /* !__NEW_HD_DRIVE_ID */
		if (id->word93 & 0x2000) {
#endif /* __NEW_HD_DRIVE_ID */
			static const masks_labels_t ultra_modes2 = {
				.masks = { 0x0800, 0x0008, 0x1000, 0x0010,
					0x2000, 0x0020, 0x4000, 0x0040,
					0x8000, 0x0080 },
				.labels = "*\0""udma3 \0""*\0""udma4 \0"
					"*\0""udma5 \0""*\0""udma6 \0"
					"*\0""udma7 \0"
			};
			print_flags(&ultra_modes2, id->dma_ultra);
		}
	}
	printf("\n AdvancedPM=%s", (!(id_regs[83] & 8)) ? "no" : "yes");
	if (id_regs[83] & 8) {
		if (!(id_regs[86] & 8))
			printf(": disabled (255)");
		else if ((id_regs[91] & 0xFF00) != 0x4000)
			printf(": unknown setting");
		else
			printf(": mode=0x%02X (%u)", id_regs[91] & 0xFF, id_regs[91] & 0xFF);
	}
	if (id_regs[82] & 0x20)
		printf(" WriteCache=%s", (id_regs[85] & 0x20) ? "enabled" : "disabled");
#ifdef __NEW_HD_DRIVE_ID
	if ((id->minor_rev_num && id->minor_rev_num <= 31)
	 || (id->major_rev_num && id->minor_rev_num <= 31)
	) {
		printf("\n Drive conforms to: %s: ",
			(id->minor_rev_num <= 31) ? nth_string(minor_str, id->minor_rev_num) : "unknown");
		if (id->major_rev_num != 0x0000 /* NOVAL_0 */
		 && id->major_rev_num != 0xFFFF /* NOVAL_1 */
		) {
			for (i = 0; i <= 15; i++) {
				if (id->major_rev_num & (1<<i))
					printf(" ATA/ATAPI-%u", i);
			}
		}
	}
#endif /* __NEW_HD_DRIVE_ID */
	puts("\n\n * current active mode\n");
}
#endif

static void flush_buffer_cache(/*int fd*/ void)
{
	fsync(fd);				/* flush buffers */
	ioctl_or_warn(fd, BLKFLSBUF, NULL); /* do it again, big time */
#ifdef HDIO_DRIVE_CMD
	sleep(1);
	if (ioctl(fd, HDIO_DRIVE_CMD, NULL) && errno != EINVAL) {	/* await completion */
		if (ENABLE_IOCTL_HEX2STR_ERROR) /* To be coherent with ioctl_or_warn */
			bb_perror_msg("HDIO_DRIVE_CMD");
		else
			bb_perror_msg("ioctl %#x failed", HDIO_DRIVE_CMD);
	}
#endif
}

static void seek_to_zero(/*int fd*/ void)
{
	xlseek(fd, (off_t) 0, SEEK_SET);
}

static void read_big_block(/*int fd,*/ char *buf)
{
	int i;

	xread(fd, buf, TIMING_BUF_BYTES);
	/* access all sectors of buf to ensure the read fully completed */
	for (i = 0; i < TIMING_BUF_BYTES; i += 512)
		buf[i] &= 1;
}

static unsigned dev_size_mb(/*int fd*/ void)
{
	union {
		unsigned long long blksize64;
		unsigned blksize32;
	} u;

	if (0 == ioctl(fd, BLKGETSIZE64, &u.blksize64)) { // bytes
		u.blksize64 /= (1024 * 1024);
	} else {
		xioctl(fd, BLKGETSIZE, &u.blksize32); // sectors
		u.blksize64 = u.blksize32 / (2 * 1024);
	}
	if (u.blksize64 > UINT_MAX)
		return UINT_MAX;
	return u.blksize64;
}

static void print_timing(unsigned m, unsigned elapsed_us)
{
	unsigned sec = elapsed_us / 1000000;
	unsigned hs = (elapsed_us % 1000000) / 10000;

	printf("%5u MB in %u.%02u seconds = %u kB/s\n",
		m, sec, hs,
		/* "| 1" prevents div-by-0 */
		(unsigned) ((unsigned long long)m * (1024 * 1000000) / (elapsed_us | 1))
		// ~= (m * 1024) / (elapsed_us / 1000000)
		// = kb / elapsed_sec
	);
}

static void do_time(int cache /*,int fd*/)
/* cache=1: time cache: repeatedly read N MB at offset 0
 * cache=0: time device: linear read, starting at offset 0
 */
{
	unsigned max_iterations, iterations;
	unsigned start; /* doesn't need to be long long */
	unsigned elapsed, elapsed2;
	unsigned total_MB;
	char *buf = xmalloc(TIMING_BUF_BYTES);

	if (mlock(buf, TIMING_BUF_BYTES))
		bb_perror_msg_and_die("mlock");

	/* Clear out the device request queues & give them time to complete.
	 * NB: *small* delay. User is expected to have a clue and to not run
	 * heavy io in parallel with measurements. */
	sync();
	sleep(1);
	if (cache) { /* Time cache */
		seek_to_zero();
		read_big_block(buf);
		printf("Timing buffer-cache reads: ");
	} else { /* Time device */
		printf("Timing buffered disk reads:");
	}
	fflush_all();

	/* Now do the timing */
	iterations = 0;
	/* Max time to run (small for cache, avoids getting
	 * huge total_MB which can overlow unsigned type) */
	elapsed2 = 510000; /* cache */
	max_iterations = UINT_MAX;
	if (!cache) {
		elapsed2 = 3000000; /* not cache */
		/* Don't want to read past the end! */
		max_iterations = dev_size_mb() / TIMING_BUF_MB;
	}
	start = monotonic_us();
	do {
		if (cache)
			seek_to_zero();
		read_big_block(buf);
		elapsed = (unsigned)monotonic_us() - start;
		++iterations;
	} while (elapsed < elapsed2 && iterations < max_iterations);
	total_MB = iterations * TIMING_BUF_MB;
	//printf(" elapsed:%u iterations:%u ", elapsed, iterations);
	if (cache) {
		/* Cache: remove lseek() and monotonic_us() overheads
		 * from elapsed */
		start = monotonic_us();
		do {
			seek_to_zero();
			elapsed2 = (unsigned)monotonic_us() - start;
		} while (--iterations);
		//printf(" elapsed2:%u ", elapsed2);
		elapsed -= elapsed2;
		total_MB *= 2; // BUFCACHE_FACTOR (why?)
		flush_buffer_cache();
	}
	print_timing(total_MB, elapsed);
	munlock(buf, TIMING_BUF_BYTES);
	free(buf);
}

#if ENABLE_FEATURE_HDPARM_HDIO_TRISTATE_HWIF
static void bus_state_value(unsigned value)
{
	if (value == BUSSTATE_ON)
		on_off(1);
	else if (value == BUSSTATE_OFF)
		on_off(0);
	else if (value == BUSSTATE_TRISTATE)
		puts(" (tristate)");
	else
		printf(" (unknown: %u)\n", value);
}
#endif

#ifdef HDIO_DRIVE_CMD
static void interpret_standby(uint8_t standby)
{
	printf(" (");
	if (standby == 0) {
		printf("off");
	} else if (standby <= 240 || standby == 252 || standby == 255) {
		/* standby is in 5 sec units */
		unsigned t = standby * 5;
		printf("%u minutes %u seconds", t / 60, t % 60);
	} else if (standby <= 251) {
		unsigned t = (standby - 240); /* t is in 30 min units */;
		printf("%u.%c hours", t / 2, (t & 1) ? '5' : '0');
	}
	if (standby == 253)
		printf("vendor-specific");
	if (standby == 254)
		printf("reserved");
	puts(")");
}

static const uint8_t xfermode_val[] ALIGN1 = {
	 8,      9,     10,     11,     12,     13,     14,     15,
	16,     17,     18,     19,     20,     21,     22,     23,
	32,     33,     34,     35,     36,     37,     38,     39,
	64,     65,     66,     67,     68,     69,     70,     71
};
/* NB: we save size by _not_ storing terninating NUL! */
static const char xfermode_name[][5] ALIGN1 = {
	"pio0", "pio1", "pio2", "pio3", "pio4", "pio5", "pio6", "pio7",
	"sdma0","sdma1","sdma2","sdma3","sdma4","sdma5","sdma6","sdma7",
	"mdma0","mdma1","mdma2","mdma3","mdma4","mdma5","mdma6","mdma7",
	"udma0","udma1","udma2","udma3","udma4","udma5","udma6","udma7"
};

static int translate_xfermode(const char *name)
{
	int val;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(xfermode_val); i++) {
		if (!strncmp(name, xfermode_name[i], 5))
			if (strlen(name) <= 5)
				return xfermode_val[i];
	}
	/* Negative numbers are invalid and are caught later */
	val = bb_strtoi(name, NULL, 10);
	if (!errno)
		return val;
	return -1;
}

static void interpret_xfermode(unsigned xfermode)
{
	printf(" (");
	if (xfermode == 0)
		printf("default PIO mode");
	else if (xfermode == 1)
		printf("default PIO mode, disable IORDY");
	else if (xfermode >= 8 && xfermode <= 15)
		printf("PIO flow control mode%u", xfermode - 8);
	else if (xfermode >= 16 && xfermode <= 23)
		printf("singleword DMA mode%u", xfermode - 16);
	else if (xfermode >= 32 && xfermode <= 39)
		printf("multiword DMA mode%u", xfermode - 32);
	else if (xfermode >= 64 && xfermode <= 71)
		printf("UltraDMA mode%u", xfermode - 64);
	else
		printf("unknown");
	puts(")");
}
#endif /* HDIO_DRIVE_CMD */

static void print_flag(int flag, const char *s, unsigned long value)
{
	if (flag)
		printf(" setting %s to %lu\n", s, value);
}

static void process_dev(char *devname)
{
	/*int fd;*/
	long parm, multcount;
#ifndef HDIO_DRIVE_CMD
	int force_operation = 0;
#endif
	/* Please restore args[n] to these values after each ioctl
	   except for args[2] */
	unsigned char args[4] = { WIN_SETFEATURES, 0, 0, 0 };
	const char *fmt = " %s\t= %2ld";

	/*fd = xopen_nonblocking(devname);*/
	xmove_fd(xopen_nonblocking(devname), fd);
	printf("\n%s:\n", devname);

	if (getset_readahead == IS_SET) {
		print_flag(getset_readahead, "fs readahead", Xreadahead);
		ioctl_or_warn(fd, BLKRASET, (int *)Xreadahead);
	}
#if ENABLE_FEATURE_HDPARM_HDIO_UNREGISTER_HWIF
	if (unregister_hwif) {
		printf(" attempting to unregister hwif#%lu\n", hwif);
		ioctl_or_warn(fd, HDIO_UNREGISTER_HWIF, (int *)(unsigned long)hwif);
	}
#endif
#if ENABLE_FEATURE_HDPARM_HDIO_SCAN_HWIF
	if (scan_hwif == IS_SET) {
		printf(" attempting to scan hwif (0x%lx, 0x%lx, %lu)\n", hwif_data, hwif_ctrl, hwif_irq);
		args[0] = hwif_data;
		args[1] = hwif_ctrl;
		args[2] = hwif_irq;
		ioctl_or_warn(fd, HDIO_SCAN_HWIF, args);
		args[0] = WIN_SETFEATURES;
		args[1] = 0;
	}
#endif
	if (set_piomode) {
		if (noisy_piomode) {
			printf(" attempting to ");
			if (piomode == 255)
				puts("auto-tune PIO mode");
			else if (piomode < 100)
				printf("set PIO mode to %d\n", piomode);
			else if (piomode < 200)
				printf("set MDMA mode to %d\n", (piomode-100));
			else
				printf("set UDMA mode to %d\n", (piomode-200));
		}
		ioctl_or_warn(fd, HDIO_SET_PIO_MODE, (int *)(unsigned long)piomode);
	}
	if (getset_io32bit == IS_SET) {
		print_flag(getset_io32bit, "32-bit IO_support flag", io32bit);
		ioctl_or_warn(fd, HDIO_SET_32BIT, (int *)io32bit);
	}
	if (getset_mult == IS_SET) {
		print_flag(getset_mult, "multcount", mult);
#ifdef HDIO_DRIVE_CMD
		ioctl_or_warn(fd, HDIO_SET_MULTCOUNT, (void *)mult);
#else
		force_operation |= (!ioctl_or_warn(fd, HDIO_SET_MULTCOUNT, (void *)mult));
#endif
	}
	if (getset_readonly == IS_SET) {
		print_flag_on_off(getset_readonly, "readonly", readonly);
		ioctl_or_warn(fd, BLKROSET, &readonly);
	}
	if (getset_unmask == IS_SET) {
		print_flag_on_off(getset_unmask, "unmaskirq", unmask);
		ioctl_or_warn(fd, HDIO_SET_UNMASKINTR, (int *)unmask);
	}
#if ENABLE_FEATURE_HDPARM_HDIO_GETSET_DMA
	if (getset_dma == IS_SET) {
		print_flag_on_off(getset_dma, "using_dma", dma);
		ioctl_or_warn(fd, HDIO_SET_DMA, (int *)dma);
	}
#endif /* FEATURE_HDPARM_HDIO_GETSET_DMA */
#ifdef HDIO_SET_QDMA
	if (getset_dma_q == IS_SET) {
		print_flag_on_off(getset_dma_q, "DMA queue_depth", dma_q);
		ioctl_or_warn(fd, HDIO_SET_QDMA, (int *)dma_q);
	}
#endif
	if (getset_nowerr == IS_SET) {
		print_flag_on_off(getset_nowerr, "nowerr", nowerr);
		ioctl_or_warn(fd, HDIO_SET_NOWERR, (int *)nowerr);
	}
	if (getset_keep == IS_SET) {
		print_flag_on_off(getset_keep, "keep_settings", keep);
		ioctl_or_warn(fd, HDIO_SET_KEEPSETTINGS, (int *)keep);
	}
#ifdef HDIO_DRIVE_CMD
	if (getset_doorlock == IS_SET) {
		args[0] = doorlock ? WIN_DOORLOCK : WIN_DOORUNLOCK;
		args[2] = 0;
		print_flag_on_off(getset_doorlock, "drive doorlock", doorlock);
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
		args[0] = WIN_SETFEATURES;
	}
	if (getset_dkeep == IS_SET) {
		/* lock/unlock the drive's "feature" settings */
		print_flag_on_off(getset_dkeep, "drive keep features", dkeep);
		args[2] = dkeep ? 0x66 : 0xcc;
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
	}
	if (getset_defects == IS_SET) {
		args[2] = defects ? 0x04 : 0x84;
		print_flag(getset_defects, "drive defect-mgmt", defects);
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
	}
	if (getset_prefetch == IS_SET) {
		args[1] = prefetch;
		args[2] = 0xab;
		print_flag(getset_prefetch, "drive prefetch", prefetch);
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
		args[1] = 0;
	}
	if (set_xfermode) {
		args[1] = xfermode_requested;
		args[2] = 3;
		print_flag(1, "xfermode", xfermode_requested);
		interpret_xfermode(xfermode_requested);
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
		args[1] = 0;
	}
	if (getset_lookahead == IS_SET) {
		args[2] = lookahead ? 0xaa : 0x55;
		print_flag_on_off(getset_lookahead, "drive read-lookahead", lookahead);
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
	}
	if (getset_apmmode == IS_SET) {
		/* feature register */
		args[2] = (apmmode == 255) ? 0x85 /* disable */ : 0x05 /* set */;
		args[1] = apmmode; /* sector count register 1-255 */
		printf(" setting APM level to %s 0x%02lX (%ld)\n",
			(apmmode == 255) ? "disabled" : "",
			apmmode, apmmode);
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
		args[1] = 0;
	}
	if (getset_wcache == IS_SET) {
#ifdef DO_FLUSHCACHE
#ifndef WIN_FLUSHCACHE
#define WIN_FLUSHCACHE 0xe7
#endif
#endif /* DO_FLUSHCACHE */
		args[2] = wcache ? 0x02 : 0x82;
		print_flag_on_off(getset_wcache, "drive write-caching", wcache);
#ifdef DO_FLUSHCACHE
		if (!wcache)
			ioctl_or_warn(fd, HDIO_DRIVE_CMD, &flushcache);
#endif /* DO_FLUSHCACHE */
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
#ifdef DO_FLUSHCACHE
		if (!wcache)
			ioctl_or_warn(fd, HDIO_DRIVE_CMD, &flushcache);
#endif /* DO_FLUSHCACHE */
	}

	/* In code below, we do not preserve args[0], but the rest
	   is preserved, including args[2] */
	args[2] = 0;

	if (set_standbynow) {
#ifndef WIN_STANDBYNOW1
#define WIN_STANDBYNOW1 0xE0
#endif
#ifndef WIN_STANDBYNOW2
#define WIN_STANDBYNOW2 0x94
#endif
		puts(" issuing standby command");
		args[0] = WIN_STANDBYNOW1;
		ioctl_alt_or_warn(HDIO_DRIVE_CMD, args, WIN_STANDBYNOW2);
	}
	if (set_sleepnow) {
#ifndef WIN_SLEEPNOW1
#define WIN_SLEEPNOW1 0xE6
#endif
#ifndef WIN_SLEEPNOW2
#define WIN_SLEEPNOW2 0x99
#endif
		puts(" issuing sleep command");
		args[0] = WIN_SLEEPNOW1;
		ioctl_alt_or_warn(HDIO_DRIVE_CMD, args, WIN_SLEEPNOW2);
	}
	if (set_seagate) {
		args[0] = 0xfb;
		puts(" disabling Seagate auto powersaving mode");
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
	}
	if (getset_standby == IS_SET) {
		args[0] = WIN_SETIDLE1;
		args[1] = standby_requested;
		print_flag(1, "standby", standby_requested);
		interpret_standby(standby_requested);
		ioctl_or_warn(fd, HDIO_DRIVE_CMD, &args);
		args[1] = 0;
	}
#else	/* HDIO_DRIVE_CMD */
	if (force_operation) {
		char buf[512];
		flush_buffer_cache();
		if (-1 == read(fd, buf, sizeof(buf)))
			bb_perror_msg("read of 512 bytes failed");
	}
#endif  /* HDIO_DRIVE_CMD */
	if (getset_mult || get_identity) {
		multcount = -1;
		if (ioctl(fd, HDIO_GET_MULTCOUNT, &multcount)) {
			/* To be coherent with ioctl_or_warn. */
			if (getset_mult && ENABLE_IOCTL_HEX2STR_ERROR)
				bb_perror_msg("HDIO_GET_MULTCOUNT");
			else
				bb_perror_msg("ioctl %#x failed", HDIO_GET_MULTCOUNT);
		} else if (getset_mult) {
			printf(fmt, "multcount", multcount);
			on_off(multcount != 0);
		}
	}
	if (getset_io32bit) {
		if (!ioctl_or_warn(fd, HDIO_GET_32BIT, &parm)) {
			printf(" IO_support\t=%3ld (", parm);
			if (parm == 0)
				puts("default 16-bit)");
			else if (parm == 2)
				puts("16-bit)");
			else if (parm == 1)
				puts("32-bit)");
			else if (parm == 3)
				puts("32-bit w/sync)");
			else if (parm == 8)
				puts("Request-Queue-Bypass)");
			else
				puts("\?\?\?)");
		}
	}
	if (getset_unmask) {
		if (!ioctl_or_warn(fd, HDIO_GET_UNMASKINTR, &parm))
			print_value_on_off("unmaskirq", parm);
	}
#if ENABLE_FEATURE_HDPARM_HDIO_GETSET_DMA
	if (getset_dma) {
		if (!ioctl_or_warn(fd, HDIO_GET_DMA, &parm)) {
			printf(fmt, "using_dma", parm);
			if (parm == 8)
				puts(" (DMA-Assisted-PIO)");
			else
				on_off(parm != 0);
		}
	}
#endif
#ifdef HDIO_GET_QDMA
	if (getset_dma_q) {
		if (!ioctl_or_warn(fd, HDIO_GET_QDMA, &parm))
			print_value_on_off("queue_depth", parm);
	}
#endif
	if (getset_keep) {
		if (!ioctl_or_warn(fd, HDIO_GET_KEEPSETTINGS, &parm))
			print_value_on_off("keepsettings", parm);
	}
	if (getset_nowerr) {
		if (!ioctl_or_warn(fd, HDIO_GET_NOWERR, &parm))
			print_value_on_off("nowerr", parm);
	}
	if (getset_readonly) {
		if (!ioctl_or_warn(fd, BLKROGET, &parm))
			print_value_on_off("readonly", parm);
	}
	if (getset_readahead) {
		if (!ioctl_or_warn(fd, BLKRAGET, &parm))
			print_value_on_off("readahead", parm);
	}
	if (get_geom) {
		if (!ioctl_or_warn(fd, BLKGETSIZE, &parm)) {
			struct hd_geometry g;

			if (!ioctl_or_warn(fd, HDIO_GETGEO, &g))
				printf(" geometry\t= %u/%u/%u, sectors = %ld, start = %ld\n",
					g.cylinders, g.heads, g.sectors, parm, g.start);
		}
	}
#ifdef HDIO_DRIVE_CMD
	if (get_powermode) {
#ifndef WIN_CHECKPOWERMODE1
#define WIN_CHECKPOWERMODE1 0xE5
#endif
#ifndef WIN_CHECKPOWERMODE2
#define WIN_CHECKPOWERMODE2 0x98
#endif
		const char *state;

		args[0] = WIN_CHECKPOWERMODE1;
		if (ioctl_alt_or_warn(HDIO_DRIVE_CMD, args, WIN_CHECKPOWERMODE2)) {
			if (errno != EIO || args[0] != 0 || args[1] != 0)
				state = "unknown";
			else
				state = "sleeping";
		} else
			state = (args[2] == 255) ? "active/idle" : "standby";
		args[1] = args[2] = 0;

		printf(" drive state is:  %s\n", state);
	}
#endif
#if ENABLE_FEATURE_HDPARM_HDIO_DRIVE_RESET
	if (perform_reset) {
		ioctl_or_warn(fd, HDIO_DRIVE_RESET, NULL);
	}
#endif /* FEATURE_HDPARM_HDIO_DRIVE_RESET */
#if ENABLE_FEATURE_HDPARM_HDIO_TRISTATE_HWIF
	if (perform_tristate) {
		args[0] = 0;
		args[1] = tristate;
		ioctl_or_warn(fd, HDIO_TRISTATE_HWIF, &args);
	}
#endif /* FEATURE_HDPARM_HDIO_TRISTATE_HWIF */
#if ENABLE_FEATURE_HDPARM_GET_IDENTITY
	if (get_identity) {
		struct hd_driveid id;

		if (!ioctl(fd, HDIO_GET_IDENTITY, &id))	{
			if (multcount != -1) {
				id.multsect = multcount;
				id.multsect_valid |= 1;
			} else
				id.multsect_valid &= ~1;
			dump_identity(&id);
		} else if (errno == -ENOMSG)
			puts(" no identification info available");
		else if (ENABLE_IOCTL_HEX2STR_ERROR)  /* To be coherent with ioctl_or_warn */
			bb_perror_msg("HDIO_GET_IDENTITY");
		else
			bb_perror_msg("ioctl %#x failed", HDIO_GET_IDENTITY);
	}

	if (get_IDentity) {
		unsigned char args1[4+512]; /* = { ... } will eat 0.5k of rodata! */

		memset(args1, 0, sizeof(args1));
		args1[0] = WIN_IDENTIFY;
		args1[3] = 1;
		if (!ioctl_alt_or_warn(HDIO_DRIVE_CMD, args1, WIN_PIDENTIFY))
			identify((void *)(args1 + 4));
	}
#endif
#if ENABLE_FEATURE_HDPARM_HDIO_TRISTATE_HWIF
	if (getset_busstate == IS_SET) {
		print_flag(1, "bus state", busstate);
		bus_state_value(busstate);
		ioctl_or_warn(fd, HDIO_SET_BUSSTATE, (int *)(unsigned long)busstate);
	}
	if (getset_busstate) {
		if (!ioctl_or_warn(fd, HDIO_GET_BUSSTATE, &parm)) {
			printf(fmt, "bus state", parm);
			bus_state_value(parm);
		}
	}
#endif
	if (reread_partn)
		ioctl_or_warn(fd, BLKRRPART, NULL);

	if (do_ctimings)
		do_time(1 /*,fd*/); /* time cache */
	if (do_timings)
		do_time(0 /*,fd*/); /* time device */
	if (do_flush)
		flush_buffer_cache();
	close(fd);
}

#if ENABLE_FEATURE_HDPARM_GET_IDENTITY
static int fromhex(unsigned char c)
{
	if (isdigit(c))
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - ('a' - 10));
	bb_error_msg_and_die("bad char: '%c' 0x%02x", c, c);
}

static void identify_from_stdin(void) NORETURN;
static void identify_from_stdin(void)
{
	uint16_t sbuf[256];
	unsigned char buf[1280];
	unsigned char *b = (unsigned char *)buf;
	int i;

	xread(STDIN_FILENO, buf, 1280);

	// Convert the newline-separated hex data into an identify block.

	for (i = 0; i < 256; i++) {
		int j;
		for (j = 0; j < 4; j++)
			sbuf[i] = (sbuf[i] << 4) + fromhex(*(b++));
	}

	// Parse the data.

	identify(sbuf);
}
#else
void identify_from_stdin(void);
#endif

/* busybox specific stuff */
static int parse_opts(unsigned long *value, int min, int max)
{
	if (optarg) {
		*value = xatol_range(optarg, min, max);
		return IS_SET;
	}
	return IS_GET;
}
static int parse_opts_0_max(unsigned long *value, int max)
{
	return parse_opts(value, 0, max);
}
static int parse_opts_0_1(unsigned long *value)
{
	return parse_opts(value, 0, 1);
}
static int parse_opts_0_INTMAX(unsigned long *value)
{
	return parse_opts(value, 0, INT_MAX);
}

static void parse_xfermode(int flag, smallint *get, smallint *set, int *value)
{
	if (flag) {
		*get = IS_GET;
		if (optarg) {
			*value = translate_xfermode(optarg);
			*set = (*value > -1);
		}
	}
}

/*------- getopt short options --------*/
static const char hdparm_options[] ALIGN1 =
	"gfu::n::p:r::m::c::k::a::B:tT"
	IF_FEATURE_HDPARM_GET_IDENTITY("iI")
	IF_FEATURE_HDPARM_HDIO_GETSET_DMA("d::")
#ifdef HDIO_DRIVE_CMD
	"S:D:P:X:K:A:L:W:CyYzZ"
#endif
	IF_FEATURE_HDPARM_HDIO_UNREGISTER_HWIF("U:")
#ifdef HDIO_GET_QDMA
#ifdef HDIO_SET_QDMA
	"Q:"
#else
	"Q"
#endif
#endif
	IF_FEATURE_HDPARM_HDIO_DRIVE_RESET("w")
	IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF("x::b:")
	IF_FEATURE_HDPARM_HDIO_SCAN_HWIF("R:");
/*-------------------------------------*/

/* our main() routine: */
int hdparm_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int hdparm_main(int argc, char **argv)
{
	int c;
	int flagcount = 0;

	INIT_G();

	while ((c = getopt(argc, argv, hdparm_options)) >= 0) {
		flagcount++;
		IF_FEATURE_HDPARM_GET_IDENTITY(get_IDentity |= (c == 'I'));
		IF_FEATURE_HDPARM_GET_IDENTITY(get_identity |= (c == 'i'));
		get_geom |= (c == 'g');
		do_flush |= (c == 'f');
		if (c == 'u') getset_unmask    = parse_opts_0_1(&unmask);
	IF_FEATURE_HDPARM_HDIO_GETSET_DMA(
		if (c == 'd') getset_dma       = parse_opts_0_max(&dma, 9);
	)
		if (c == 'n') getset_nowerr    = parse_opts_0_1(&nowerr);
		parse_xfermode((c == 'p'), &noisy_piomode, &set_piomode, &piomode);
		if (c == 'r') getset_readonly  = parse_opts_0_1(&readonly);
		if (c == 'm') getset_mult      = parse_opts_0_INTMAX(&mult /*32*/);
		if (c == 'c') getset_io32bit   = parse_opts_0_INTMAX(&io32bit /*8*/);
		if (c == 'k') getset_keep      = parse_opts_0_1(&keep);
		if (c == 'a') getset_readahead = parse_opts_0_INTMAX(&Xreadahead);
		if (c == 'B') getset_apmmode   = parse_opts(&apmmode, 1, 255);
		do_flush |= do_timings |= (c == 't');
		do_flush |= do_ctimings |= (c == 'T');
#ifdef HDIO_DRIVE_CMD
		if (c == 'S') getset_standby  = parse_opts_0_max(&standby_requested, 255);
		if (c == 'D') getset_defects  = parse_opts_0_INTMAX(&defects);
		if (c == 'P') getset_prefetch = parse_opts_0_INTMAX(&prefetch);
		parse_xfermode((c == 'X'), &get_xfermode, &set_xfermode, &xfermode_requested);
		if (c == 'K') getset_dkeep     = parse_opts_0_1(&prefetch);
		if (c == 'A') getset_lookahead = parse_opts_0_1(&lookahead);
		if (c == 'L') getset_doorlock  = parse_opts_0_1(&doorlock);
		if (c == 'W') getset_wcache    = parse_opts_0_1(&wcache);
		get_powermode |= (c == 'C');
		set_standbynow |= (c == 'y');
		set_sleepnow |= (c == 'Y');
		reread_partn |= (c == 'z');
		set_seagate |= (c == 'Z');
#endif
		IF_FEATURE_HDPARM_HDIO_UNREGISTER_HWIF(if (c == 'U') unregister_hwif = parse_opts_0_INTMAX(&hwif));
#ifdef HDIO_GET_QDMA
		if (c == 'Q') {
			getset_dma_q = parse_opts_0_INTMAX(&dma_q);
		}
#endif
		IF_FEATURE_HDPARM_HDIO_DRIVE_RESET(perform_reset = (c == 'r'));
		IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF(if (c == 'x') perform_tristate = parse_opts_0_1(&tristate));
		IF_FEATURE_HDPARM_HDIO_TRISTATE_HWIF(if (c == 'b') getset_busstate = parse_opts_0_max(&busstate, 2));
#if ENABLE_FEATURE_HDPARM_HDIO_SCAN_HWIF
		if (c == 'R') {
			scan_hwif = parse_opts_0_INTMAX(&hwif_data);
			hwif_ctrl = xatoi_positive((argv[optind]) ? argv[optind] : "");
			hwif_irq  = xatoi_positive((argv[optind+1]) ? argv[optind+1] : "");
			/* Move past the 2 additional arguments */
			argv += 2;
			argc -= 2;
		}
#endif
	}
	/* When no flags are given (flagcount = 0), -acdgkmnru is assumed. */
	if (!flagcount) {
		getset_mult = getset_io32bit = getset_unmask = getset_keep = getset_readonly = getset_readahead = get_geom = IS_GET;
		IF_FEATURE_HDPARM_HDIO_GETSET_DMA(getset_dma = IS_GET);
	}
	argv += optind;

	if (!*argv) {
		if (ENABLE_FEATURE_HDPARM_GET_IDENTITY && !isatty(STDIN_FILENO))
			identify_from_stdin(); /* EXIT */
		bb_show_usage();
	}

	do {
		process_dev(*argv++);
	} while (*argv);

	return EXIT_SUCCESS;
}
