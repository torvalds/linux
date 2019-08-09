/*======================================================================

    Aironet driver for 4500 and 4800 series cards

    This code is released under both the GPL version 2 and BSD licenses.
    Either license may be used.  The respective licenses are found at
    the end of this file.

    This code was developed by Benjamin Reed <breed@users.sourceforge.net>
    including portions of which come from the Aironet PC4500
    Developer's Reference Manual and used with permission.  Copyright
    (C) 1999 Benjamin Reed.  All Rights Reserved.  Permission to use
    code in the Developer's manual was granted for this driver by
    Aironet.  Major code contributions were received from Javier Achirica
    <achirica@users.sourceforge.net> and Jean Tourrilhes <jt@hpl.hp.com>.
    Code was also integrated from the Cisco Aironet driver for Linux.
    Support for MPI350 cards was added by Fabrice Bellet
    <fabrice@bellet.info>.

======================================================================*/

#include <linux/err.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/bitops.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/io.h>
#include <asm/unaligned.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <crypto/aes.h>
#include <crypto/skcipher.h>

#include <net/cfg80211.h>
#include <net/iw_handler.h>

#include "airo.h"

#define DRV_NAME "airo"

#ifdef CONFIG_PCI
static const struct pci_device_id card_ids[] = {
	{ 0x14b9, 1, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0x4500, PCI_ANY_ID, PCI_ANY_ID },
	{ 0x14b9, 0x4800, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0x0340, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0x0350, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0x5000, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0xa504, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, card_ids);

static int airo_pci_probe(struct pci_dev *, const struct pci_device_id *);
static void airo_pci_remove(struct pci_dev *);
static int airo_pci_suspend(struct pci_dev *pdev, pm_message_t state);
static int airo_pci_resume(struct pci_dev *pdev);

static struct pci_driver airo_driver = {
	.name     = DRV_NAME,
	.id_table = card_ids,
	.probe    = airo_pci_probe,
	.remove   = airo_pci_remove,
	.suspend  = airo_pci_suspend,
	.resume   = airo_pci_resume,
};
#endif /* CONFIG_PCI */

/* Include Wireless Extension definition and check version - Jean II */
#include <linux/wireless.h>
#define WIRELESS_SPY		/* enable iwspy support */

#define CISCO_EXT		/* enable Cisco extensions */
#ifdef CISCO_EXT
#include <linux/delay.h>
#endif

/* Hack to do some power saving */
#define POWER_ON_DOWN

/* As you can see this list is HUGH!
   I really don't know what a lot of these counts are about, but they
   are all here for completeness.  If the IGNLABEL macro is put in
   infront of the label, that statistic will not be included in the list
   of statistics in the /proc filesystem */

#define IGNLABEL(comment) NULL
static const char *statsLabels[] = {
	"RxOverrun",
	IGNLABEL("RxPlcpCrcErr"),
	IGNLABEL("RxPlcpFormatErr"),
	IGNLABEL("RxPlcpLengthErr"),
	"RxMacCrcErr",
	"RxMacCrcOk",
	"RxWepErr",
	"RxWepOk",
	"RetryLong",
	"RetryShort",
	"MaxRetries",
	"NoAck",
	"NoCts",
	"RxAck",
	"RxCts",
	"TxAck",
	"TxRts",
	"TxCts",
	"TxMc",
	"TxBc",
	"TxUcFrags",
	"TxUcPackets",
	"TxBeacon",
	"RxBeacon",
	"TxSinColl",
	"TxMulColl",
	"DefersNo",
	"DefersProt",
	"DefersEngy",
	"DupFram",
	"RxFragDisc",
	"TxAged",
	"RxAged",
	"LostSync-MaxRetry",
	"LostSync-MissedBeacons",
	"LostSync-ArlExceeded",
	"LostSync-Deauth",
	"LostSync-Disassoced",
	"LostSync-TsfTiming",
	"HostTxMc",
	"HostTxBc",
	"HostTxUc",
	"HostTxFail",
	"HostRxMc",
	"HostRxBc",
	"HostRxUc",
	"HostRxDiscard",
	IGNLABEL("HmacTxMc"),
	IGNLABEL("HmacTxBc"),
	IGNLABEL("HmacTxUc"),
	IGNLABEL("HmacTxFail"),
	IGNLABEL("HmacRxMc"),
	IGNLABEL("HmacRxBc"),
	IGNLABEL("HmacRxUc"),
	IGNLABEL("HmacRxDiscard"),
	IGNLABEL("HmacRxAccepted"),
	"SsidMismatch",
	"ApMismatch",
	"RatesMismatch",
	"AuthReject",
	"AuthTimeout",
	"AssocReject",
	"AssocTimeout",
	IGNLABEL("ReasonOutsideTable"),
	IGNLABEL("ReasonStatus1"),
	IGNLABEL("ReasonStatus2"),
	IGNLABEL("ReasonStatus3"),
	IGNLABEL("ReasonStatus4"),
	IGNLABEL("ReasonStatus5"),
	IGNLABEL("ReasonStatus6"),
	IGNLABEL("ReasonStatus7"),
	IGNLABEL("ReasonStatus8"),
	IGNLABEL("ReasonStatus9"),
	IGNLABEL("ReasonStatus10"),
	IGNLABEL("ReasonStatus11"),
	IGNLABEL("ReasonStatus12"),
	IGNLABEL("ReasonStatus13"),
	IGNLABEL("ReasonStatus14"),
	IGNLABEL("ReasonStatus15"),
	IGNLABEL("ReasonStatus16"),
	IGNLABEL("ReasonStatus17"),
	IGNLABEL("ReasonStatus18"),
	IGNLABEL("ReasonStatus19"),
	"RxMan",
	"TxMan",
	"RxRefresh",
	"TxRefresh",
	"RxPoll",
	"TxPoll",
	"HostRetries",
	"LostSync-HostReq",
	"HostTxBytes",
	"HostRxBytes",
	"ElapsedUsec",
	"ElapsedSec",
	"LostSyncBetterAP",
	"PrivacyMismatch",
	"Jammed",
	"DiscRxNotWepped",
	"PhyEleMismatch",
	(char*)-1 };
#ifndef RUN_AT
#define RUN_AT(x) (jiffies+(x))
#endif


/* These variables are for insmod, since it seems that the rates
   can only be set in setup_card.  Rates should be a comma separated
   (no spaces) list of rates (up to 8). */

static int rates[8];
static char *ssids[3];

static int io[4];
static int irq[4];

static
int maxencrypt /* = 0 */; /* The highest rate that the card can encrypt at.
		       0 means no limit.  For old cards this was 4 */

static int auto_wep /* = 0 */; /* If set, it tries to figure out the wep mode */
static int aux_bap /* = 0 */; /* Checks to see if the aux ports are needed to read
		    the bap, needed on some older cards and buses. */
static int adhoc;

static int probe = 1;

static kuid_t proc_kuid;
static int proc_uid /* = 0 */;

static kgid_t proc_kgid;
static int proc_gid /* = 0 */;

static int airo_perm = 0555;

static int proc_perm = 0644;

MODULE_AUTHOR("Benjamin Reed");
MODULE_DESCRIPTION("Support for Cisco/Aironet 802.11 wireless ethernet cards.  "
		   "Direct support for ISA/PCI/MPI cards and support for PCMCIA when used with airo_cs.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_SUPPORTED_DEVICE("Aironet 4500, 4800 and Cisco 340/350");
module_param_hw_array(io, int, ioport, NULL, 0);
module_param_hw_array(irq, int, irq, NULL, 0);
module_param_array(rates, int, NULL, 0);
module_param_array(ssids, charp, NULL, 0);
module_param(auto_wep, int, 0);
MODULE_PARM_DESC(auto_wep,
		 "If non-zero, the driver will keep looping through the authentication options until an association is made.  "
		 "The value of auto_wep is number of the wep keys to check.  "
		 "A value of 2 will try using the key at index 0 and index 1.");
module_param(aux_bap, int, 0);
MODULE_PARM_DESC(aux_bap,
		 "If non-zero, the driver will switch into a mode that seems to work better for older cards with some older buses.  "
		 "Before switching it checks that the switch is needed.");
module_param(maxencrypt, int, 0);
MODULE_PARM_DESC(maxencrypt,
		 "The maximum speed that the card can do encryption.  "
		 "Units are in 512kbs.  "
		 "Zero (default) means there is no limit.  "
		 "Older cards used to be limited to 2mbs (4).");
module_param(adhoc, int, 0);
MODULE_PARM_DESC(adhoc, "If non-zero, the card will start in adhoc mode.");
module_param(probe, int, 0);
MODULE_PARM_DESC(probe, "If zero, the driver won't start the card.");

module_param(proc_uid, int, 0);
MODULE_PARM_DESC(proc_uid, "The uid that the /proc files will belong to.");
module_param(proc_gid, int, 0);
MODULE_PARM_DESC(proc_gid, "The gid that the /proc files will belong to.");
module_param(airo_perm, int, 0);
MODULE_PARM_DESC(airo_perm, "The permission bits of /proc/[driver/]aironet.");
module_param(proc_perm, int, 0);
MODULE_PARM_DESC(proc_perm, "The permission bits of the files in /proc");

/* This is a kind of sloppy hack to get this information to OUT4500 and
   IN4500.  I would be extremely interested in the situation where this
   doesn't work though!!! */
static int do8bitIO /* = 0 */;

/* Return codes */
#define SUCCESS 0
#define ERROR -1
#define NO_PACKET -2

/* Commands */
#define NOP2		0x0000
#define MAC_ENABLE	0x0001
#define MAC_DISABLE	0x0002
#define CMD_LOSE_SYNC	0x0003 /* Not sure what this does... */
#define CMD_SOFTRESET	0x0004
#define HOSTSLEEP	0x0005
#define CMD_MAGIC_PKT	0x0006
#define CMD_SETWAKEMASK	0x0007
#define CMD_READCFG	0x0008
#define CMD_SETMODE	0x0009
#define CMD_ALLOCATETX	0x000a
#define CMD_TRANSMIT	0x000b
#define CMD_DEALLOCATETX 0x000c
#define NOP		0x0010
#define CMD_WORKAROUND	0x0011
#define CMD_ALLOCATEAUX 0x0020
#define CMD_ACCESS	0x0021
#define CMD_PCIBAP	0x0022
#define CMD_PCIAUX	0x0023
#define CMD_ALLOCBUF	0x0028
#define CMD_GETTLV	0x0029
#define CMD_PUTTLV	0x002a
#define CMD_DELTLV	0x002b
#define CMD_FINDNEXTTLV	0x002c
#define CMD_PSPNODES	0x0030
#define CMD_SETCW	0x0031    
#define CMD_SETPCF	0x0032    
#define CMD_SETPHYREG	0x003e
#define CMD_TXTEST	0x003f
#define MAC_ENABLETX	0x0101
#define CMD_LISTBSS	0x0103
#define CMD_SAVECFG	0x0108
#define CMD_ENABLEAUX	0x0111
#define CMD_WRITERID	0x0121
#define CMD_USEPSPNODES	0x0130
#define MAC_ENABLERX	0x0201

/* Command errors */
#define ERROR_QUALIF 0x00
#define ERROR_ILLCMD 0x01
#define ERROR_ILLFMT 0x02
#define ERROR_INVFID 0x03
#define ERROR_INVRID 0x04
#define ERROR_LARGE 0x05
#define ERROR_NDISABL 0x06
#define ERROR_ALLOCBSY 0x07
#define ERROR_NORD 0x0B
#define ERROR_NOWR 0x0C
#define ERROR_INVFIDTX 0x0D
#define ERROR_TESTACT 0x0E
#define ERROR_TAGNFND 0x12
#define ERROR_DECODE 0x20
#define ERROR_DESCUNAV 0x21
#define ERROR_BADLEN 0x22
#define ERROR_MODE 0x80
#define ERROR_HOP 0x81
#define ERROR_BINTER 0x82
#define ERROR_RXMODE 0x83
#define ERROR_MACADDR 0x84
#define ERROR_RATES 0x85
#define ERROR_ORDER 0x86
#define ERROR_SCAN 0x87
#define ERROR_AUTH 0x88
#define ERROR_PSMODE 0x89
#define ERROR_RTYPE 0x8A
#define ERROR_DIVER 0x8B
#define ERROR_SSID 0x8C
#define ERROR_APLIST 0x8D
#define ERROR_AUTOWAKE 0x8E
#define ERROR_LEAP 0x8F

/* Registers */
#define COMMAND 0x00
#define PARAM0 0x02
#define PARAM1 0x04
#define PARAM2 0x06
#define STATUS 0x08
#define RESP0 0x0a
#define RESP1 0x0c
#define RESP2 0x0e
#define LINKSTAT 0x10
#define SELECT0 0x18
#define OFFSET0 0x1c
#define RXFID 0x20
#define TXALLOCFID 0x22
#define TXCOMPLFID 0x24
#define DATA0 0x36
#define EVSTAT 0x30
#define EVINTEN 0x32
#define EVACK 0x34
#define SWS0 0x28
#define SWS1 0x2a
#define SWS2 0x2c
#define SWS3 0x2e
#define AUXPAGE 0x3A
#define AUXOFF 0x3C
#define AUXDATA 0x3E

#define FID_TX 1
#define FID_RX 2
/* Offset into aux memory for descriptors */
#define AUX_OFFSET 0x800
/* Size of allocated packets */
#define PKTSIZE 1840
#define RIDSIZE 2048
/* Size of the transmit queue */
#define MAXTXQ 64

/* BAP selectors */
#define BAP0 0 /* Used for receiving packets */
#define BAP1 2 /* Used for xmiting packets and working with RIDS */

/* Flags */
#define COMMAND_BUSY 0x8000

#define BAP_BUSY 0x8000
#define BAP_ERR 0x4000
#define BAP_DONE 0x2000

#define PROMISC 0xffff
#define NOPROMISC 0x0000

#define EV_CMD 0x10
#define EV_CLEARCOMMANDBUSY 0x4000
#define EV_RX 0x01
#define EV_TX 0x02
#define EV_TXEXC 0x04
#define EV_ALLOC 0x08
#define EV_LINK 0x80
#define EV_AWAKE 0x100
#define EV_TXCPY 0x400
#define EV_UNKNOWN 0x800
#define EV_MIC 0x1000 /* Message Integrity Check Interrupt */
#define EV_AWAKEN 0x2000
#define STATUS_INTS (EV_AWAKE|EV_LINK|EV_TXEXC|EV_TX|EV_TXCPY|EV_RX|EV_MIC)

#ifdef CHECK_UNKNOWN_INTS
#define IGNORE_INTS ( EV_CMD | EV_UNKNOWN)
#else
#define IGNORE_INTS (~STATUS_INTS)
#endif

/* RID TYPES */
#define RID_RW 0x20

/* The RIDs */
#define RID_CAPABILITIES 0xFF00
#define RID_APINFO     0xFF01
#define RID_RADIOINFO  0xFF02
#define RID_UNKNOWN3   0xFF03
#define RID_RSSI       0xFF04
#define RID_CONFIG     0xFF10
#define RID_SSID       0xFF11
#define RID_APLIST     0xFF12
#define RID_DRVNAME    0xFF13
#define RID_ETHERENCAP 0xFF14
#define RID_WEP_TEMP   0xFF15
#define RID_WEP_PERM   0xFF16
#define RID_MODULATION 0xFF17
#define RID_OPTIONS    0xFF18
#define RID_ACTUALCONFIG 0xFF20 /*readonly*/
#define RID_FACTORYCONFIG 0xFF21
#define RID_UNKNOWN22  0xFF22
#define RID_LEAPUSERNAME 0xFF23
#define RID_LEAPPASSWORD 0xFF24
#define RID_STATUS     0xFF50
#define RID_BEACON_HST 0xFF51
#define RID_BUSY_HST   0xFF52
#define RID_RETRIES_HST 0xFF53
#define RID_UNKNOWN54  0xFF54
#define RID_UNKNOWN55  0xFF55
#define RID_UNKNOWN56  0xFF56
#define RID_MIC        0xFF57
#define RID_STATS16    0xFF60
#define RID_STATS16DELTA 0xFF61
#define RID_STATS16DELTACLEAR 0xFF62
#define RID_STATS      0xFF68
#define RID_STATSDELTA 0xFF69
#define RID_STATSDELTACLEAR 0xFF6A
#define RID_ECHOTEST_RID 0xFF70
#define RID_ECHOTEST_RESULTS 0xFF71
#define RID_BSSLISTFIRST 0xFF72
#define RID_BSSLISTNEXT  0xFF73
#define RID_WPA_BSSLISTFIRST 0xFF74
#define RID_WPA_BSSLISTNEXT  0xFF75

typedef struct {
	u16 cmd;
	u16 parm0;
	u16 parm1;
	u16 parm2;
} Cmd;

typedef struct {
	u16 status;
	u16 rsp0;
	u16 rsp1;
	u16 rsp2;
} Resp;

/*
 * Rids and endian-ness:  The Rids will always be in cpu endian, since
 * this all the patches from the big-endian guys end up doing that.
 * so all rid access should use the read/writeXXXRid routines.
 */

/* This structure came from an email sent to me from an engineer at
   aironet for inclusion into this driver */
typedef struct WepKeyRid WepKeyRid;
struct WepKeyRid {
	__le16 len;
	__le16 kindex;
	u8 mac[ETH_ALEN];
	__le16 klen;
	u8 key[16];
} __packed;

/* These structures are from the Aironet's PC4500 Developers Manual */
typedef struct Ssid Ssid;
struct Ssid {
	__le16 len;
	u8 ssid[32];
} __packed;

typedef struct SsidRid SsidRid;
struct SsidRid {
	__le16 len;
	Ssid ssids[3];
} __packed;

typedef struct ModulationRid ModulationRid;
struct ModulationRid {
        __le16 len;
        __le16 modulation;
#define MOD_DEFAULT cpu_to_le16(0)
#define MOD_CCK cpu_to_le16(1)
#define MOD_MOK cpu_to_le16(2)
} __packed;

typedef struct ConfigRid ConfigRid;
struct ConfigRid {
	__le16 len; /* sizeof(ConfigRid) */
	__le16 opmode; /* operating mode */
#define MODE_STA_IBSS cpu_to_le16(0)
#define MODE_STA_ESS cpu_to_le16(1)
#define MODE_AP cpu_to_le16(2)
#define MODE_AP_RPTR cpu_to_le16(3)
#define MODE_CFG_MASK cpu_to_le16(0xff)
#define MODE_ETHERNET_HOST cpu_to_le16(0<<8) /* rx payloads converted */
#define MODE_LLC_HOST cpu_to_le16(1<<8) /* rx payloads left as is */
#define MODE_AIRONET_EXTEND cpu_to_le16(1<<9) /* enable Aironet extenstions */
#define MODE_AP_INTERFACE cpu_to_le16(1<<10) /* enable ap interface extensions */
#define MODE_ANTENNA_ALIGN cpu_to_le16(1<<11) /* enable antenna alignment */
#define MODE_ETHER_LLC cpu_to_le16(1<<12) /* enable ethernet LLC */
#define MODE_LEAF_NODE cpu_to_le16(1<<13) /* enable leaf node bridge */
#define MODE_CF_POLLABLE cpu_to_le16(1<<14) /* enable CF pollable */
#define MODE_MIC cpu_to_le16(1<<15) /* enable MIC */
	__le16 rmode; /* receive mode */
#define RXMODE_BC_MC_ADDR cpu_to_le16(0)
#define RXMODE_BC_ADDR cpu_to_le16(1) /* ignore multicasts */
#define RXMODE_ADDR cpu_to_le16(2) /* ignore multicast and broadcast */
#define RXMODE_RFMON cpu_to_le16(3) /* wireless monitor mode */
#define RXMODE_RFMON_ANYBSS cpu_to_le16(4)
#define RXMODE_LANMON cpu_to_le16(5) /* lan style monitor -- data packets only */
#define RXMODE_MASK cpu_to_le16(255)
#define RXMODE_DISABLE_802_3_HEADER cpu_to_le16(1<<8) /* disables 802.3 header on rx */
#define RXMODE_FULL_MASK (RXMODE_MASK | RXMODE_DISABLE_802_3_HEADER)
#define RXMODE_NORMALIZED_RSSI cpu_to_le16(1<<9) /* return normalized RSSI */
	__le16 fragThresh;
	__le16 rtsThres;
	u8 macAddr[ETH_ALEN];
	u8 rates[8];
	__le16 shortRetryLimit;
	__le16 longRetryLimit;
	__le16 txLifetime; /* in kusec */
	__le16 rxLifetime; /* in kusec */
	__le16 stationary;
	__le16 ordering;
	__le16 u16deviceType; /* for overriding device type */
	__le16 cfpRate;
	__le16 cfpDuration;
	__le16 _reserved1[3];
	/*---------- Scanning/Associating ----------*/
	__le16 scanMode;
#define SCANMODE_ACTIVE cpu_to_le16(0)
#define SCANMODE_PASSIVE cpu_to_le16(1)
#define SCANMODE_AIROSCAN cpu_to_le16(2)
	__le16 probeDelay; /* in kusec */
	__le16 probeEnergyTimeout; /* in kusec */
        __le16 probeResponseTimeout;
	__le16 beaconListenTimeout;
	__le16 joinNetTimeout;
	__le16 authTimeout;
	__le16 authType;
#define AUTH_OPEN cpu_to_le16(0x1)
#define AUTH_ENCRYPT cpu_to_le16(0x101)
#define AUTH_SHAREDKEY cpu_to_le16(0x102)
#define AUTH_ALLOW_UNENCRYPTED cpu_to_le16(0x200)
	__le16 associationTimeout;
	__le16 specifiedApTimeout;
	__le16 offlineScanInterval;
	__le16 offlineScanDuration;
	__le16 linkLossDelay;
	__le16 maxBeaconLostTime;
	__le16 refreshInterval;
#define DISABLE_REFRESH cpu_to_le16(0xFFFF)
	__le16 _reserved1a[1];
	/*---------- Power save operation ----------*/
	__le16 powerSaveMode;
#define POWERSAVE_CAM cpu_to_le16(0)
#define POWERSAVE_PSP cpu_to_le16(1)
#define POWERSAVE_PSPCAM cpu_to_le16(2)
	__le16 sleepForDtims;
	__le16 listenInterval;
	__le16 fastListenInterval;
	__le16 listenDecay;
	__le16 fastListenDelay;
	__le16 _reserved2[2];
	/*---------- Ap/Ibss config items ----------*/
	__le16 beaconPeriod;
	__le16 atimDuration;
	__le16 hopPeriod;
	__le16 channelSet;
	__le16 channel;
	__le16 dtimPeriod;
	__le16 bridgeDistance;
	__le16 radioID;
	/*---------- Radio configuration ----------*/
	__le16 radioType;
#define RADIOTYPE_DEFAULT cpu_to_le16(0)
#define RADIOTYPE_802_11 cpu_to_le16(1)
#define RADIOTYPE_LEGACY cpu_to_le16(2)
	u8 rxDiversity;
	u8 txDiversity;
	__le16 txPower;
#define TXPOWER_DEFAULT 0
	__le16 rssiThreshold;
#define RSSI_DEFAULT 0
        __le16 modulation;
#define PREAMBLE_AUTO cpu_to_le16(0)
#define PREAMBLE_LONG cpu_to_le16(1)
#define PREAMBLE_SHORT cpu_to_le16(2)
	__le16 preamble;
	__le16 homeProduct;
	__le16 radioSpecific;
	/*---------- Aironet Extensions ----------*/
	u8 nodeName[16];
	__le16 arlThreshold;
	__le16 arlDecay;
	__le16 arlDelay;
	__le16 _reserved4[1];
	/*---------- Aironet Extensions ----------*/
	u8 magicAction;
#define MAGIC_ACTION_STSCHG 1
#define MAGIC_ACTION_RESUME 2
#define MAGIC_IGNORE_MCAST (1<<8)
#define MAGIC_IGNORE_BCAST (1<<9)
#define MAGIC_SWITCH_TO_PSP (0<<10)
#define MAGIC_STAY_IN_CAM (1<<10)
	u8 magicControl;
	__le16 autoWake;
} __packed;

typedef struct StatusRid StatusRid;
struct StatusRid {
	__le16 len;
	u8 mac[ETH_ALEN];
	__le16 mode;
	__le16 errorCode;
	__le16 sigQuality;
	__le16 SSIDlen;
	char SSID[32];
	char apName[16];
	u8 bssid[4][ETH_ALEN];
	__le16 beaconPeriod;
	__le16 dimPeriod;
	__le16 atimDuration;
	__le16 hopPeriod;
	__le16 channelSet;
	__le16 channel;
	__le16 hopsToBackbone;
	__le16 apTotalLoad;
	__le16 generatedLoad;
	__le16 accumulatedArl;
	__le16 signalQuality;
	__le16 currentXmitRate;
	__le16 apDevExtensions;
	__le16 normalizedSignalStrength;
	__le16 shortPreamble;
	u8 apIP[4];
	u8 noisePercent; /* Noise percent in last second */
	u8 noisedBm; /* Noise dBm in last second */
	u8 noiseAvePercent; /* Noise percent in last minute */
	u8 noiseAvedBm; /* Noise dBm in last minute */
	u8 noiseMaxPercent; /* Highest noise percent in last minute */
	u8 noiseMaxdBm; /* Highest noise dbm in last minute */
	__le16 load;
	u8 carrier[4];
	__le16 assocStatus;
#define STAT_NOPACKETS 0
#define STAT_NOCARRIERSET 10
#define STAT_GOTCARRIERSET 11
#define STAT_WRONGSSID 20
#define STAT_BADCHANNEL 25
#define STAT_BADBITRATES 30
#define STAT_BADPRIVACY 35
#define STAT_APFOUND 40
#define STAT_APREJECTED 50
#define STAT_AUTHENTICATING 60
#define STAT_DEAUTHENTICATED 61
#define STAT_AUTHTIMEOUT 62
#define STAT_ASSOCIATING 70
#define STAT_DEASSOCIATED 71
#define STAT_ASSOCTIMEOUT 72
#define STAT_NOTAIROAP 73
#define STAT_ASSOCIATED 80
#define STAT_LEAPING 90
#define STAT_LEAPFAILED 91
#define STAT_LEAPTIMEDOUT 92
#define STAT_LEAPCOMPLETE 93
} __packed;

typedef struct StatsRid StatsRid;
struct StatsRid {
	__le16 len;
	__le16 spacer;
	__le32 vals[100];
} __packed;

typedef struct APListRid APListRid;
struct APListRid {
	__le16 len;
	u8 ap[4][ETH_ALEN];
} __packed;

typedef struct CapabilityRid CapabilityRid;
struct CapabilityRid {
	__le16 len;
	char oui[3];
	char zero;
	__le16 prodNum;
	char manName[32];
	char prodName[16];
	char prodVer[8];
	char factoryAddr[ETH_ALEN];
	char aironetAddr[ETH_ALEN];
	__le16 radioType;
	__le16 country;
	char callid[ETH_ALEN];
	char supportedRates[8];
	char rxDiversity;
	char txDiversity;
	__le16 txPowerLevels[8];
	__le16 hardVer;
	__le16 hardCap;
	__le16 tempRange;
	__le16 softVer;
	__le16 softSubVer;
	__le16 interfaceVer;
	__le16 softCap;
	__le16 bootBlockVer;
	__le16 requiredHard;
	__le16 extSoftCap;
} __packed;

/* Only present on firmware >= 5.30.17 */
typedef struct BSSListRidExtra BSSListRidExtra;
struct BSSListRidExtra {
  __le16 unknown[4];
  u8 fixed[12]; /* WLAN management frame */
  u8 iep[624];
} __packed;

typedef struct BSSListRid BSSListRid;
struct BSSListRid {
  __le16 len;
  __le16 index; /* First is 0 and 0xffff means end of list */
#define RADIO_FH 1 /* Frequency hopping radio type */
#define RADIO_DS 2 /* Direct sequence radio type */
#define RADIO_TMA 4 /* Proprietary radio used in old cards (2500) */
  __le16 radioType;
  u8 bssid[ETH_ALEN]; /* Mac address of the BSS */
  u8 zero;
  u8 ssidLen;
  u8 ssid[32];
  __le16 dBm;
#define CAP_ESS cpu_to_le16(1<<0)
#define CAP_IBSS cpu_to_le16(1<<1)
#define CAP_PRIVACY cpu_to_le16(1<<4)
#define CAP_SHORTHDR cpu_to_le16(1<<5)
  __le16 cap;
  __le16 beaconInterval;
  u8 rates[8]; /* Same as rates for config rid */
  struct { /* For frequency hopping only */
    __le16 dwell;
    u8 hopSet;
    u8 hopPattern;
    u8 hopIndex;
    u8 fill;
  } fh;
  __le16 dsChannel;
  __le16 atimWindow;

  /* Only present on firmware >= 5.30.17 */
  BSSListRidExtra extra;
} __packed;

typedef struct {
  BSSListRid bss;
  struct list_head list;
} BSSListElement;

typedef struct tdsRssiEntry tdsRssiEntry;
struct tdsRssiEntry {
  u8 rssipct;
  u8 rssidBm;
} __packed;

typedef struct tdsRssiRid tdsRssiRid;
struct tdsRssiRid {
  u16 len;
  tdsRssiEntry x[256];
} __packed;

typedef struct MICRid MICRid;
struct MICRid {
	__le16 len;
	__le16 state;
	__le16 multicastValid;
	u8  multicast[16];
	__le16 unicastValid;
	u8  unicast[16];
} __packed;

typedef struct MICBuffer MICBuffer;
struct MICBuffer {
	__be16 typelen;

	union {
	    u8 snap[8];
	    struct {
		u8 dsap;
		u8 ssap;
		u8 control;
		u8 orgcode[3];
		u8 fieldtype[2];
	    } llc;
	} u;
	__be32 mic;
	__be32 seq;
} __packed;

typedef struct {
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
} etherHead;

#define TXCTL_TXOK (1<<1) /* report if tx is ok */
#define TXCTL_TXEX (1<<2) /* report if tx fails */
#define TXCTL_802_3 (0<<3) /* 802.3 packet */
#define TXCTL_802_11 (1<<3) /* 802.11 mac packet */
#define TXCTL_ETHERNET (0<<4) /* payload has ethertype */
#define TXCTL_LLC (1<<4) /* payload is llc */
#define TXCTL_RELEASE (0<<5) /* release after completion */
#define TXCTL_NORELEASE (1<<5) /* on completion returns to host */

#define BUSY_FID 0x10000

#ifdef CISCO_EXT
#define AIROMAGIC	0xa55a
/* Warning : SIOCDEVPRIVATE may disapear during 2.5.X - Jean II */
#ifdef SIOCIWFIRSTPRIV
#ifdef SIOCDEVPRIVATE
#define AIROOLDIOCTL	SIOCDEVPRIVATE
#define AIROOLDIDIFC 	AIROOLDIOCTL + 1
#endif /* SIOCDEVPRIVATE */
#else /* SIOCIWFIRSTPRIV */
#define SIOCIWFIRSTPRIV SIOCDEVPRIVATE
#endif /* SIOCIWFIRSTPRIV */
/* This may be wrong. When using the new SIOCIWFIRSTPRIV range, we probably
 * should use only "GET" ioctls (last bit set to 1). "SET" ioctls are root
 * only and don't return the modified struct ifreq to the application which
 * is usually a problem. - Jean II */
#define AIROIOCTL	SIOCIWFIRSTPRIV
#define AIROIDIFC 	AIROIOCTL + 1

/* Ioctl constants to be used in airo_ioctl.command */

#define	AIROGCAP  		0	// Capability rid
#define AIROGCFG		1       // USED A LOT
#define AIROGSLIST		2	// System ID list
#define AIROGVLIST		3       // List of specified AP's
#define AIROGDRVNAM		4	//  NOTUSED
#define AIROGEHTENC		5	// NOTUSED
#define AIROGWEPKTMP		6
#define AIROGWEPKNV		7
#define AIROGSTAT		8
#define AIROGSTATSC32		9
#define AIROGSTATSD32		10
#define AIROGMICRID		11
#define AIROGMICSTATS		12
#define AIROGFLAGS		13
#define AIROGID			14
#define AIRORRID		15
#define AIRORSWVERSION		17

/* Leave gap of 40 commands after AIROGSTATSD32 for future */

#define AIROPCAP               	AIROGSTATSD32 + 40
#define AIROPVLIST              AIROPCAP      + 1
#define AIROPSLIST		AIROPVLIST    + 1
#define AIROPCFG		AIROPSLIST    + 1
#define AIROPSIDS		AIROPCFG      + 1
#define AIROPAPLIST		AIROPSIDS     + 1
#define AIROPMACON		AIROPAPLIST   + 1	/* Enable mac  */
#define AIROPMACOFF		AIROPMACON    + 1 	/* Disable mac */
#define AIROPSTCLR		AIROPMACOFF   + 1
#define AIROPWEPKEY		AIROPSTCLR    + 1
#define AIROPWEPKEYNV		AIROPWEPKEY   + 1
#define AIROPLEAPPWD            AIROPWEPKEYNV + 1
#define AIROPLEAPUSR            AIROPLEAPPWD  + 1

/* Flash codes */

#define AIROFLSHRST	       AIROPWEPKEYNV  + 40
#define AIROFLSHGCHR           AIROFLSHRST    + 1
#define AIROFLSHSTFL           AIROFLSHGCHR   + 1
#define AIROFLSHPCHR           AIROFLSHSTFL   + 1
#define AIROFLPUTBUF           AIROFLSHPCHR   + 1
#define AIRORESTART            AIROFLPUTBUF   + 1

#define FLASHSIZE	32768
#define AUXMEMSIZE	(256 * 1024)

typedef struct aironet_ioctl {
	unsigned short command;		// What to do
	unsigned short len;		// Len of data
	unsigned short ridnum;		// rid number
	unsigned char __user *data;	// d-data
} aironet_ioctl;

static const char swversion[] = "2.1";
#endif /* CISCO_EXT */

#define NUM_MODULES       2
#define MIC_MSGLEN_MAX    2400
#define EMMH32_MSGLEN_MAX MIC_MSGLEN_MAX
#define AIRO_DEF_MTU      2312

typedef struct {
	u32   size;            // size
	u8    enabled;         // MIC enabled or not
	u32   rxSuccess;       // successful packets received
	u32   rxIncorrectMIC;  // pkts dropped due to incorrect MIC comparison
	u32   rxNotMICed;      // pkts dropped due to not being MIC'd
	u32   rxMICPlummed;    // pkts dropped due to not having a MIC plummed
	u32   rxWrongSequence; // pkts dropped due to sequence number violation
	u32   reserve[32];
} mic_statistics;

typedef struct {
	__be32 coeff[((EMMH32_MSGLEN_MAX)+3)>>2];
	u64 accum;	// accumulated mic, reduced to u32 in final()
	int position;	// current position (byte offset) in message
	union {
		u8  d8[4];
		__be32 d32;
	} part;	// saves partial message word across update() calls
} emmh32_context;

typedef struct {
	emmh32_context seed;	    // Context - the seed
	u32		 rx;	    // Received sequence number
	u32		 tx;	    // Tx sequence number
	u32		 window;    // Start of window
	u8		 valid;	    // Flag to say if context is valid or not
	u8		 key[16];
} miccntx;

typedef struct {
	miccntx mCtx;		// Multicast context
	miccntx uCtx;		// Unicast context
} mic_module;

typedef struct {
	unsigned int  rid: 16;
	unsigned int  len: 15;
	unsigned int  valid: 1;
	dma_addr_t host_addr;
} Rid;

typedef struct {
	unsigned int  offset: 15;
	unsigned int  eoc: 1;
	unsigned int  len: 15;
	unsigned int  valid: 1;
	dma_addr_t host_addr;
} TxFid;

struct rx_hdr {
	__le16 status, len;
	u8 rssi[2];
	u8 rate;
	u8 freq;
	__le16 tmp[4];
} __packed;

typedef struct {
	unsigned int  ctl: 15;
	unsigned int  rdy: 1;
	unsigned int  len: 15;
	unsigned int  valid: 1;
	dma_addr_t host_addr;
} RxFid;

/*
 * Host receive descriptor
 */
typedef struct {
	unsigned char __iomem *card_ram_off; /* offset into card memory of the
						desc */
	RxFid         rx_desc;		     /* card receive descriptor */
	char          *virtual_host_addr;    /* virtual address of host receive
					        buffer */
	int           pending;
} HostRxDesc;

/*
 * Host transmit descriptor
 */
typedef struct {
	unsigned char __iomem *card_ram_off;	     /* offset into card memory of the
						desc */
	TxFid         tx_desc;		     /* card transmit descriptor */
	char          *virtual_host_addr;    /* virtual address of host receive
					        buffer */
	int           pending;
} HostTxDesc;

/*
 * Host RID descriptor
 */
typedef struct {
	unsigned char __iomem *card_ram_off;      /* offset into card memory of the
					     descriptor */
	Rid           rid_desc;		  /* card RID descriptor */
	char          *virtual_host_addr; /* virtual address of host receive
					     buffer */
} HostRidDesc;

typedef struct {
	u16 sw0;
	u16 sw1;
	u16 status;
	u16 len;
#define HOST_SET (1 << 0)
#define HOST_INT_TX (1 << 1) /* Interrupt on successful TX */
#define HOST_INT_TXERR (1 << 2) /* Interrupt on unseccessful TX */
#define HOST_LCC_PAYLOAD (1 << 4) /* LLC payload, 0 = Ethertype */
#define HOST_DONT_RLSE (1 << 5) /* Don't release buffer when done */
#define HOST_DONT_RETRY (1 << 6) /* Don't retry trasmit */
#define HOST_CLR_AID (1 << 7) /* clear AID failure */
#define HOST_RTS (1 << 9) /* Force RTS use */
#define HOST_SHORT (1 << 10) /* Do short preamble */
	u16 ctl;
	u16 aid;
	u16 retries;
	u16 fill;
} TxCtlHdr;

typedef struct {
        u16 ctl;
        u16 duration;
        char addr1[6];
        char addr2[6];
        char addr3[6];
        u16 seq;
        char addr4[6];
} WifiHdr;


typedef struct {
	TxCtlHdr ctlhdr;
	u16 fill1;
	u16 fill2;
	WifiHdr wifihdr;
	u16 gaplen;
	u16 status;
} WifiCtlHdr;

static WifiCtlHdr wifictlhdr8023 = {
	.ctlhdr = {
		.ctl	= HOST_DONT_RLSE,
	}
};

// A few details needed for WEP (Wireless Equivalent Privacy)
#define MAX_KEY_SIZE 13			// 128 (?) bits
#define MIN_KEY_SIZE  5			// 40 bits RC4 - WEP
typedef struct wep_key_t {
	u16	len;
	u8	key[16];	/* 40-bit and 104-bit keys */
} wep_key_t;

/* List of Wireless Handlers (new API) */
static const struct iw_handler_def	airo_handler_def;

static const char version[] = "airo.c 0.6 (Ben Reed & Javier Achirica)";

struct airo_info;

static int get_dec_u16( char *buffer, int *start, int limit );
static void OUT4500( struct airo_info *, u16 reg, u16 value );
static unsigned short IN4500( struct airo_info *, u16 reg );
static u16 setup_card(struct airo_info*, u8 *mac, int lock);
static int enable_MAC(struct airo_info *ai, int lock);
static void disable_MAC(struct airo_info *ai, int lock);
static void enable_interrupts(struct airo_info*);
static void disable_interrupts(struct airo_info*);
static u16 issuecommand(struct airo_info*, Cmd *pCmd, Resp *pRsp);
static int bap_setup(struct airo_info*, u16 rid, u16 offset, int whichbap);
static int aux_bap_read(struct airo_info*, __le16 *pu16Dst, int bytelen,
			int whichbap);
static int fast_bap_read(struct airo_info*, __le16 *pu16Dst, int bytelen,
			 int whichbap);
static int bap_write(struct airo_info*, const __le16 *pu16Src, int bytelen,
		     int whichbap);
static int PC4500_accessrid(struct airo_info*, u16 rid, u16 accmd);
static int PC4500_readrid(struct airo_info*, u16 rid, void *pBuf, int len, int lock);
static int PC4500_writerid(struct airo_info*, u16 rid, const void
			   *pBuf, int len, int lock);
static int do_writerid( struct airo_info*, u16 rid, const void *rid_data,
			int len, int dummy );
static u16 transmit_allocate(struct airo_info*, int lenPayload, int raw);
static int transmit_802_3_packet(struct airo_info*, int len, char *pPacket);
static int transmit_802_11_packet(struct airo_info*, int len, char *pPacket);

static int mpi_send_packet (struct net_device *dev);
static void mpi_unmap_card(struct pci_dev *pci);
static void mpi_receive_802_3(struct airo_info *ai);
static void mpi_receive_802_11(struct airo_info *ai);
static int waitbusy (struct airo_info *ai);

static irqreturn_t airo_interrupt( int irq, void* dev_id);
static int airo_thread(void *data);
static void timer_func( struct net_device *dev );
static int airo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static struct iw_statistics *airo_get_wireless_stats (struct net_device *dev);
static void airo_read_wireless_stats (struct airo_info *local);
#ifdef CISCO_EXT
static int readrids(struct net_device *dev, aironet_ioctl *comp);
static int writerids(struct net_device *dev, aironet_ioctl *comp);
static int flashcard(struct net_device *dev, aironet_ioctl *comp);
#endif /* CISCO_EXT */
static void micinit(struct airo_info *ai);
static int micsetup(struct airo_info *ai);
static int encapsulate(struct airo_info *ai, etherHead *pPacket, MICBuffer *buffer, int len);
static int decapsulate(struct airo_info *ai, MICBuffer *mic, etherHead *pPacket, u16 payLen);

static u8 airo_rssi_to_dbm (tdsRssiEntry *rssi_rid, u8 rssi);
static u8 airo_dbm_to_pct (tdsRssiEntry *rssi_rid, u8 dbm);

static void airo_networks_free(struct airo_info *ai);

struct airo_info {
	struct net_device             *dev;
	struct list_head              dev_list;
	/* Note, we can have MAX_FIDS outstanding.  FIDs are 16-bits, so we
	   use the high bit to mark whether it is in use. */
#define MAX_FIDS 6
#define MPI_MAX_FIDS 1
	u32                           fids[MAX_FIDS];
	ConfigRid config;
	char keyindex; // Used with auto wep
	char defindex; // Used with auto wep
	struct proc_dir_entry *proc_entry;
        spinlock_t aux_lock;
#define FLAG_RADIO_OFF	0	/* User disabling of MAC */
#define FLAG_RADIO_DOWN	1	/* ifup/ifdown disabling of MAC */
#define FLAG_RADIO_MASK 0x03
#define FLAG_ENABLED	2
#define FLAG_ADHOC	3	/* Needed by MIC */
#define FLAG_MIC_CAPABLE 4
#define FLAG_UPDATE_MULTI 5
#define FLAG_UPDATE_UNI 6
#define FLAG_802_11	7
#define FLAG_PROMISC	8	/* IFF_PROMISC 0x100 - include/linux/if.h */
#define FLAG_PENDING_XMIT 9
#define FLAG_PENDING_XMIT11 10
#define FLAG_MPI	11
#define FLAG_REGISTERED	12
#define FLAG_COMMIT	13
#define FLAG_RESET	14
#define FLAG_FLASHING	15
#define FLAG_WPA_CAPABLE	16
	unsigned long flags;
#define JOB_DIE	0
#define JOB_XMIT	1
#define JOB_XMIT11	2
#define JOB_STATS	3
#define JOB_PROMISC	4
#define JOB_MIC	5
#define JOB_EVENT	6
#define JOB_AUTOWEP	7
#define JOB_WSTATS	8
#define JOB_SCAN_RESULTS  9
	unsigned long jobs;
	int (*bap_read)(struct airo_info*, __le16 *pu16Dst, int bytelen,
			int whichbap);
	unsigned short *flash;
	tdsRssiEntry *rssi;
	struct task_struct *list_bss_task;
	struct task_struct *airo_thread_task;
	struct semaphore sem;
	wait_queue_head_t thr_wait;
	unsigned long expires;
	struct {
		struct sk_buff *skb;
		int fid;
	} xmit, xmit11;
	struct net_device *wifidev;
	struct iw_statistics	wstats;		// wireless stats
	unsigned long		scan_timeout;	/* Time scan should be read */
	struct iw_spy_data	spy_data;
	struct iw_public_data	wireless_data;
	/* MIC stuff */
	struct crypto_sync_skcipher	*tfm;
	mic_module		mod[2];
	mic_statistics		micstats;
	HostRxDesc rxfids[MPI_MAX_FIDS]; // rx/tx/config MPI350 descriptors
	HostTxDesc txfids[MPI_MAX_FIDS];
	HostRidDesc config_desc;
	unsigned long ridbus; // phys addr of config_desc
	struct sk_buff_head txq;// tx queue used by mpi350 code
	struct pci_dev          *pci;
	unsigned char		__iomem *pcimem;
	unsigned char		__iomem *pciaux;
	unsigned char		*shared;
	dma_addr_t		shared_dma;
	pm_message_t		power;
	SsidRid			*SSID;
	APListRid		APList;
#define	PCI_SHARED_LEN		2*MPI_MAX_FIDS*PKTSIZE+RIDSIZE
	char			proc_name[IFNAMSIZ];

	int			wep_capable;
	int			max_wep_idx;
	int			last_auth;

	/* WPA-related stuff */
	unsigned int bssListFirst;
	unsigned int bssListNext;
	unsigned int bssListRidLen;

	struct list_head network_list;
	struct list_head network_free_list;
	BSSListElement *networks;
};

static inline int bap_read(struct airo_info *ai, __le16 *pu16Dst, int bytelen,
			   int whichbap)
{
	return ai->bap_read(ai, pu16Dst, bytelen, whichbap);
}

static int setup_proc_entry( struct net_device *dev,
			     struct airo_info *apriv );
static int takedown_proc_entry( struct net_device *dev,
				struct airo_info *apriv );

static int cmdreset(struct airo_info *ai);
static int setflashmode (struct airo_info *ai);
static int flashgchar(struct airo_info *ai,int matchbyte,int dwelltime);
static int flashputbuf(struct airo_info *ai);
static int flashrestart(struct airo_info *ai,struct net_device *dev);

#define airo_print(type, name, fmt, args...) \
	printk(type DRV_NAME "(%s): " fmt "\n", name, ##args)

#define airo_print_info(name, fmt, args...) \
	airo_print(KERN_INFO, name, fmt, ##args)

#define airo_print_dbg(name, fmt, args...) \
	airo_print(KERN_DEBUG, name, fmt, ##args)

#define airo_print_warn(name, fmt, args...) \
	airo_print(KERN_WARNING, name, fmt, ##args)

#define airo_print_err(name, fmt, args...) \
	airo_print(KERN_ERR, name, fmt, ##args)

#define AIRO_FLASH(dev) (((struct airo_info *)dev->ml_priv)->flash)

/***********************************************************************
 *                              MIC ROUTINES                           *
 ***********************************************************************
 */

static int RxSeqValid (struct airo_info *ai,miccntx *context,int mcast,u32 micSeq);
static void MoveWindow(miccntx *context, u32 micSeq);
static void emmh32_setseed(emmh32_context *context, u8 *pkey, int keylen,
			   struct crypto_sync_skcipher *tfm);
static void emmh32_init(emmh32_context *context);
static void emmh32_update(emmh32_context *context, u8 *pOctets, int len);
static void emmh32_final(emmh32_context *context, u8 digest[4]);
static int flashpchar(struct airo_info *ai,int byte,int dwelltime);

static void age_mic_context(miccntx *cur, miccntx *old, u8 *key, int key_len,
			    struct crypto_sync_skcipher *tfm)
{
	/* If the current MIC context is valid and its key is the same as
	 * the MIC register, there's nothing to do.
	 */
	if (cur->valid && (memcmp(cur->key, key, key_len) == 0))
		return;

	/* Age current mic Context */
	memcpy(old, cur, sizeof(*cur));

	/* Initialize new context */
	memcpy(cur->key, key, key_len);
	cur->window  = 33; /* Window always points to the middle */
	cur->rx      = 0;  /* Rx Sequence numbers */
	cur->tx      = 0;  /* Tx sequence numbers */
	cur->valid   = 1;  /* Key is now valid */

	/* Give key to mic seed */
	emmh32_setseed(&cur->seed, key, key_len, tfm);
}

/* micinit - Initialize mic seed */

static void micinit(struct airo_info *ai)
{
	MICRid mic_rid;

	clear_bit(JOB_MIC, &ai->jobs);
	PC4500_readrid(ai, RID_MIC, &mic_rid, sizeof(mic_rid), 0);
	up(&ai->sem);

	ai->micstats.enabled = (le16_to_cpu(mic_rid.state) & 0x00FF) ? 1 : 0;
	if (!ai->micstats.enabled) {
		/* So next time we have a valid key and mic is enabled, we will
		 * update the sequence number if the key is the same as before.
		 */
		ai->mod[0].uCtx.valid = 0;
		ai->mod[0].mCtx.valid = 0;
		return;
	}

	if (mic_rid.multicastValid) {
		age_mic_context(&ai->mod[0].mCtx, &ai->mod[1].mCtx,
		                mic_rid.multicast, sizeof(mic_rid.multicast),
		                ai->tfm);
	}

	if (mic_rid.unicastValid) {
		age_mic_context(&ai->mod[0].uCtx, &ai->mod[1].uCtx,
				mic_rid.unicast, sizeof(mic_rid.unicast),
				ai->tfm);
	}
}

/* micsetup - Get ready for business */

static int micsetup(struct airo_info *ai) {
	int i;

	if (ai->tfm == NULL)
		ai->tfm = crypto_alloc_sync_skcipher("ctr(aes)", 0, 0);

        if (IS_ERR(ai->tfm)) {
                airo_print_err(ai->dev->name, "failed to load transform for AES");
                ai->tfm = NULL;
                return ERROR;
        }

	for (i=0; i < NUM_MODULES; i++) {
		memset(&ai->mod[i].mCtx,0,sizeof(miccntx));
		memset(&ai->mod[i].uCtx,0,sizeof(miccntx));
	}
	return SUCCESS;
}

static const u8 micsnap[] = {0xAA,0xAA,0x03,0x00,0x40,0x96,0x00,0x02};

/*===========================================================================
 * Description: Mic a packet
 *    
 *      Inputs: etherHead * pointer to an 802.3 frame
 *    
 *     Returns: BOOLEAN if successful, otherwise false.
 *             PacketTxLen will be updated with the mic'd packets size.
 *
 *    Caveats: It is assumed that the frame buffer will already
 *             be big enough to hold the largets mic message possible.
 *            (No memory allocation is done here).
 *  
 *    Author: sbraneky (10/15/01)
 *    Merciless hacks by rwilcher (1/14/02)
 */

static int encapsulate(struct airo_info *ai ,etherHead *frame, MICBuffer *mic, int payLen)
{
	miccntx   *context;

	// Determine correct context
	// If not adhoc, always use unicast key

	if (test_bit(FLAG_ADHOC, &ai->flags) && (frame->da[0] & 0x1))
		context = &ai->mod[0].mCtx;
	else
		context = &ai->mod[0].uCtx;
  
	if (!context->valid)
		return ERROR;

	mic->typelen = htons(payLen + 16); //Length of Mic'd packet

	memcpy(&mic->u.snap, micsnap, sizeof(micsnap)); // Add Snap

	// Add Tx sequence
	mic->seq = htonl(context->tx);
	context->tx += 2;

	emmh32_init(&context->seed); // Mic the packet
	emmh32_update(&context->seed,frame->da,ETH_ALEN * 2); // DA,SA
	emmh32_update(&context->seed,(u8*)&mic->typelen,10); // Type/Length and Snap
	emmh32_update(&context->seed,(u8*)&mic->seq,sizeof(mic->seq)); //SEQ
	emmh32_update(&context->seed,(u8*)(frame + 1),payLen); //payload
	emmh32_final(&context->seed, (u8*)&mic->mic);

	/*    New Type/length ?????????? */
	mic->typelen = 0; //Let NIC know it could be an oversized packet
	return SUCCESS;
}

typedef enum {
    NONE,
    NOMIC,
    NOMICPLUMMED,
    SEQUENCE,
    INCORRECTMIC,
} mic_error;

/*===========================================================================
 *  Description: Decapsulates a MIC'd packet and returns the 802.3 packet
 *               (removes the MIC stuff) if packet is a valid packet.
 *      
 *       Inputs: etherHead  pointer to the 802.3 packet             
 *     
 *      Returns: BOOLEAN - TRUE if packet should be dropped otherwise FALSE
 *     
 *      Author: sbraneky (10/15/01)
 *    Merciless hacks by rwilcher (1/14/02)
 *---------------------------------------------------------------------------
 */

static int decapsulate(struct airo_info *ai, MICBuffer *mic, etherHead *eth, u16 payLen)
{
	int      i;
	u32      micSEQ;
	miccntx  *context;
	u8       digest[4];
	mic_error micError = NONE;

	// Check if the packet is a Mic'd packet

	if (!ai->micstats.enabled) {
		//No Mic set or Mic OFF but we received a MIC'd packet.
		if (memcmp ((u8*)eth + 14, micsnap, sizeof(micsnap)) == 0) {
			ai->micstats.rxMICPlummed++;
			return ERROR;
		}
		return SUCCESS;
	}

	if (ntohs(mic->typelen) == 0x888E)
		return SUCCESS;

	if (memcmp (mic->u.snap, micsnap, sizeof(micsnap)) != 0) {
	    // Mic enabled but packet isn't Mic'd
		ai->micstats.rxMICPlummed++;
	    	return ERROR;
	}

	micSEQ = ntohl(mic->seq);            //store SEQ as CPU order

	//At this point we a have a mic'd packet and mic is enabled
	//Now do the mic error checking.

	//Receive seq must be odd
	if ( (micSEQ & 1) == 0 ) {
		ai->micstats.rxWrongSequence++;
		return ERROR;
	}

	for (i = 0; i < NUM_MODULES; i++) {
		int mcast = eth->da[0] & 1;
		//Determine proper context 
		context = mcast ? &ai->mod[i].mCtx : &ai->mod[i].uCtx;
	
		//Make sure context is valid
		if (!context->valid) {
			if (i == 0)
				micError = NOMICPLUMMED;
			continue;                
		}
	       	//DeMic it 

		if (!mic->typelen)
			mic->typelen = htons(payLen + sizeof(MICBuffer) - 2);
	
		emmh32_init(&context->seed);
		emmh32_update(&context->seed, eth->da, ETH_ALEN*2); 
		emmh32_update(&context->seed, (u8 *)&mic->typelen, sizeof(mic->typelen)+sizeof(mic->u.snap)); 
		emmh32_update(&context->seed, (u8 *)&mic->seq,sizeof(mic->seq));	
		emmh32_update(&context->seed, (u8 *)(eth + 1),payLen);	
		//Calculate MIC
		emmh32_final(&context->seed, digest);
	
		if (memcmp(digest, &mic->mic, 4)) { //Make sure the mics match
		  //Invalid Mic
			if (i == 0)
				micError = INCORRECTMIC;
			continue;
		}

		//Check Sequence number if mics pass
		if (RxSeqValid(ai, context, mcast, micSEQ) == SUCCESS) {
			ai->micstats.rxSuccess++;
			return SUCCESS;
		}
		if (i == 0)
			micError = SEQUENCE;
	}

	// Update statistics
	switch (micError) {
		case NOMICPLUMMED: ai->micstats.rxMICPlummed++;   break;
		case SEQUENCE:    ai->micstats.rxWrongSequence++; break;
		case INCORRECTMIC: ai->micstats.rxIncorrectMIC++; break;
		case NONE:  break;
		case NOMIC: break;
	}
	return ERROR;
}

/*===========================================================================
 * Description:  Checks the Rx Seq number to make sure it is valid
 *               and hasn't already been received
 *   
 *     Inputs: miccntx - mic context to check seq against
 *             micSeq  - the Mic seq number
 *   
 *    Returns: TRUE if valid otherwise FALSE. 
 *
 *    Author: sbraneky (10/15/01)
 *    Merciless hacks by rwilcher (1/14/02)
 *---------------------------------------------------------------------------
 */

static int RxSeqValid (struct airo_info *ai,miccntx *context,int mcast,u32 micSeq)
{
	u32 seq,index;

	//Allow for the ap being rebooted - if it is then use the next 
	//sequence number of the current sequence number - might go backwards

	if (mcast) {
		if (test_bit(FLAG_UPDATE_MULTI, &ai->flags)) {
			clear_bit (FLAG_UPDATE_MULTI, &ai->flags);
			context->window = (micSeq > 33) ? micSeq : 33;
			context->rx     = 0;        // Reset rx
		}
	} else if (test_bit(FLAG_UPDATE_UNI, &ai->flags)) {
		clear_bit (FLAG_UPDATE_UNI, &ai->flags);
		context->window = (micSeq > 33) ? micSeq : 33; // Move window
		context->rx     = 0;        // Reset rx
	}

	//Make sequence number relative to START of window
	seq = micSeq - (context->window - 33);

	//Too old of a SEQ number to check.
	if ((s32)seq < 0)
		return ERROR;
    
	if ( seq > 64 ) {
		//Window is infinite forward
		MoveWindow(context,micSeq);
		return SUCCESS;
	}

	// We are in the window. Now check the context rx bit to see if it was already sent
	seq >>= 1;         //divide by 2 because we only have odd numbers
	index = 1 << seq;  //Get an index number

	if (!(context->rx & index)) {
		//micSEQ falls inside the window.
		//Add seqence number to the list of received numbers.
		context->rx |= index;

		MoveWindow(context,micSeq);

		return SUCCESS;
	}
	return ERROR;
}

static void MoveWindow(miccntx *context, u32 micSeq)
{
	u32 shift;

	//Move window if seq greater than the middle of the window
	if (micSeq > context->window) {
		shift = (micSeq - context->window) >> 1;
    
		    //Shift out old
		if (shift < 32)
			context->rx >>= shift;
		else
			context->rx = 0;

		context->window = micSeq;      //Move window
	}
}

/*==============================================*/
/*========== EMMH ROUTINES  ====================*/
/*==============================================*/

/* mic accumulate */
#define MIC_ACCUM(val)	\
	context->accum += (u64)(val) * be32_to_cpu(context->coeff[coeff_position++]);

/* expand the key to fill the MMH coefficient array */
static void emmh32_setseed(emmh32_context *context, u8 *pkey, int keylen,
			   struct crypto_sync_skcipher *tfm)
{
  /* take the keying material, expand if necessary, truncate at 16-bytes */
  /* run through AES counter mode to generate context->coeff[] */
  
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, tfm);
	struct scatterlist sg;
	u8 iv[AES_BLOCK_SIZE] = {};
	int ret;

	crypto_sync_skcipher_setkey(tfm, pkey, 16);

	memset(context->coeff, 0, sizeof(context->coeff));
	sg_init_one(&sg, context->coeff, sizeof(context->coeff));

	skcipher_request_set_sync_tfm(req, tfm);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, sizeof(context->coeff), iv);

	ret = crypto_skcipher_encrypt(req);
	WARN_ON_ONCE(ret);
}

/* prepare for calculation of a new mic */
static void emmh32_init(emmh32_context *context)
{
	/* prepare for new mic calculation */
	context->accum = 0;
	context->position = 0;
}

/* add some bytes to the mic calculation */
static void emmh32_update(emmh32_context *context, u8 *pOctets, int len)
{
	int	coeff_position, byte_position;
  
	if (len == 0) return;
  
	coeff_position = context->position >> 2;
  
	/* deal with partial 32-bit word left over from last update */
	byte_position = context->position & 3;
	if (byte_position) {
		/* have a partial word in part to deal with */
		do {
			if (len == 0) return;
			context->part.d8[byte_position++] = *pOctets++;
			context->position++;
			len--;
		} while (byte_position < 4);
		MIC_ACCUM(ntohl(context->part.d32));
	}

	/* deal with full 32-bit words */
	while (len >= 4) {
		MIC_ACCUM(ntohl(*(__be32 *)pOctets));
		context->position += 4;
		pOctets += 4;
		len -= 4;
	}

	/* deal with partial 32-bit word that will be left over from this update */
	byte_position = 0;
	while (len > 0) {
		context->part.d8[byte_position++] = *pOctets++;
		context->position++;
		len--;
	}
}

/* mask used to zero empty bytes for final partial word */
static u32 mask32[4] = { 0x00000000L, 0xFF000000L, 0xFFFF0000L, 0xFFFFFF00L };

/* calculate the mic */
static void emmh32_final(emmh32_context *context, u8 digest[4])
{
	int	coeff_position, byte_position;
	u32	val;
  
	u64 sum, utmp;
	s64 stmp;

	coeff_position = context->position >> 2;
  
	/* deal with partial 32-bit word left over from last update */
	byte_position = context->position & 3;
	if (byte_position) {
		/* have a partial word in part to deal with */
		val = ntohl(context->part.d32);
		MIC_ACCUM(val & mask32[byte_position]);	/* zero empty bytes */
	}

	/* reduce the accumulated u64 to a 32-bit MIC */
	sum = context->accum;
	stmp = (sum  & 0xffffffffLL) - ((sum >> 32)  * 15);
	utmp = (stmp & 0xffffffffLL) - ((stmp >> 32) * 15);
	sum = utmp & 0xffffffffLL;
	if (utmp > 0x10000000fLL)
		sum -= 15;

	val = (u32)sum;
	digest[0] = (val>>24) & 0xFF;
	digest[1] = (val>>16) & 0xFF;
	digest[2] = (val>>8) & 0xFF;
	digest[3] = val & 0xFF;
}

static int readBSSListRid(struct airo_info *ai, int first,
		      BSSListRid *list)
{
	Cmd cmd;
	Resp rsp;

	if (first == 1) {
		if (ai->flags & FLAG_RADIO_MASK) return -ENETDOWN;
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd=CMD_LISTBSS;
		if (down_interruptible(&ai->sem))
			return -ERESTARTSYS;
		ai->list_bss_task = current;
		issuecommand(ai, &cmd, &rsp);
		up(&ai->sem);
		/* Let the command take effect */
		schedule_timeout_uninterruptible(3 * HZ);
		ai->list_bss_task = NULL;
	}
	return PC4500_readrid(ai, first ? ai->bssListFirst : ai->bssListNext,
			    list, ai->bssListRidLen, 1);
}

static int readWepKeyRid(struct airo_info *ai, WepKeyRid *wkr, int temp, int lock)
{
	return PC4500_readrid(ai, temp ? RID_WEP_TEMP : RID_WEP_PERM,
				wkr, sizeof(*wkr), lock);
}

static int writeWepKeyRid(struct airo_info *ai, WepKeyRid *wkr, int perm, int lock)
{
	int rc;
	rc = PC4500_writerid(ai, RID_WEP_TEMP, wkr, sizeof(*wkr), lock);
	if (rc!=SUCCESS)
		airo_print_err(ai->dev->name, "WEP_TEMP set %x", rc);
	if (perm) {
		rc = PC4500_writerid(ai, RID_WEP_PERM, wkr, sizeof(*wkr), lock);
		if (rc!=SUCCESS)
			airo_print_err(ai->dev->name, "WEP_PERM set %x", rc);
	}
	return rc;
}

static int readSsidRid(struct airo_info*ai, SsidRid *ssidr)
{
	return PC4500_readrid(ai, RID_SSID, ssidr, sizeof(*ssidr), 1);
}

static int writeSsidRid(struct airo_info*ai, SsidRid *pssidr, int lock)
{
	return PC4500_writerid(ai, RID_SSID, pssidr, sizeof(*pssidr), lock);
}

static int readConfigRid(struct airo_info *ai, int lock)
{
	int rc;
	ConfigRid cfg;

	if (ai->config.len)
		return SUCCESS;

	rc = PC4500_readrid(ai, RID_ACTUALCONFIG, &cfg, sizeof(cfg), lock);
	if (rc != SUCCESS)
		return rc;

	ai->config = cfg;
	return SUCCESS;
}

static inline void checkThrottle(struct airo_info *ai)
{
	int i;
/* Old hardware had a limit on encryption speed */
	if (ai->config.authType != AUTH_OPEN && maxencrypt) {
		for(i=0; i<8; i++) {
			if (ai->config.rates[i] > maxencrypt) {
				ai->config.rates[i] = 0;
			}
		}
	}
}

static int writeConfigRid(struct airo_info *ai, int lock)
{
	ConfigRid cfgr;

	if (!test_bit (FLAG_COMMIT, &ai->flags))
		return SUCCESS;

	clear_bit (FLAG_COMMIT, &ai->flags);
	clear_bit (FLAG_RESET, &ai->flags);
	checkThrottle(ai);
	cfgr = ai->config;

	if ((cfgr.opmode & MODE_CFG_MASK) == MODE_STA_IBSS)
		set_bit(FLAG_ADHOC, &ai->flags);
	else
		clear_bit(FLAG_ADHOC, &ai->flags);

	return PC4500_writerid( ai, RID_CONFIG, &cfgr, sizeof(cfgr), lock);
}

static int readStatusRid(struct airo_info *ai, StatusRid *statr, int lock)
{
	return PC4500_readrid(ai, RID_STATUS, statr, sizeof(*statr), lock);
}

static int writeAPListRid(struct airo_info *ai, APListRid *aplr, int lock)
{
	return PC4500_writerid(ai, RID_APLIST, aplr, sizeof(*aplr), lock);
}

static int readCapabilityRid(struct airo_info *ai, CapabilityRid *capr, int lock)
{
	return PC4500_readrid(ai, RID_CAPABILITIES, capr, sizeof(*capr), lock);
}

static int readStatsRid(struct airo_info*ai, StatsRid *sr, int rid, int lock)
{
	return PC4500_readrid(ai, rid, sr, sizeof(*sr), lock);
}

static void try_auto_wep(struct airo_info *ai)
{
	if (auto_wep && !test_bit(FLAG_RADIO_DOWN, &ai->flags)) {
		ai->expires = RUN_AT(3*HZ);
		wake_up_interruptible(&ai->thr_wait);
	}
}

static int airo_open(struct net_device *dev) {
	struct airo_info *ai = dev->ml_priv;
	int rc = 0;

	if (test_bit(FLAG_FLASHING, &ai->flags))
		return -EIO;

	/* Make sure the card is configured.
	 * Wireless Extensions may postpone config changes until the card
	 * is open (to pipeline changes and speed-up card setup). If
	 * those changes are not yet committed, do it now - Jean II */
	if (test_bit(FLAG_COMMIT, &ai->flags)) {
		disable_MAC(ai, 1);
		writeConfigRid(ai, 1);
	}

	if (ai->wifidev != dev) {
		clear_bit(JOB_DIE, &ai->jobs);
		ai->airo_thread_task = kthread_run(airo_thread, dev, "%s",
						   dev->name);
		if (IS_ERR(ai->airo_thread_task))
			return (int)PTR_ERR(ai->airo_thread_task);

		rc = request_irq(dev->irq, airo_interrupt, IRQF_SHARED,
			dev->name, dev);
		if (rc) {
			airo_print_err(dev->name,
				"register interrupt %d failed, rc %d",
				dev->irq, rc);
			set_bit(JOB_DIE, &ai->jobs);
			kthread_stop(ai->airo_thread_task);
			return rc;
		}

		/* Power on the MAC controller (which may have been disabled) */
		clear_bit(FLAG_RADIO_DOWN, &ai->flags);
		enable_interrupts(ai);

		try_auto_wep(ai);
	}
	enable_MAC(ai, 1);

	netif_start_queue(dev);
	return 0;
}

static netdev_tx_t mpi_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	int npacks, pending;
	unsigned long flags;
	struct airo_info *ai = dev->ml_priv;

	if (!skb) {
		airo_print_err(dev->name, "%s: skb == NULL!",__func__);
		return NETDEV_TX_OK;
	}
	npacks = skb_queue_len (&ai->txq);

	if (npacks >= MAXTXQ - 1) {
		netif_stop_queue (dev);
		if (npacks > MAXTXQ) {
			dev->stats.tx_fifo_errors++;
			return NETDEV_TX_BUSY;
		}
		skb_queue_tail (&ai->txq, skb);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&ai->aux_lock, flags);
	skb_queue_tail (&ai->txq, skb);
	pending = test_bit(FLAG_PENDING_XMIT, &ai->flags);
	spin_unlock_irqrestore(&ai->aux_lock,flags);
	netif_wake_queue (dev);

	if (pending == 0) {
		set_bit(FLAG_PENDING_XMIT, &ai->flags);
		mpi_send_packet (dev);
	}
	return NETDEV_TX_OK;
}

/*
 * @mpi_send_packet
 *
 * Attempt to transmit a packet. Can be called from interrupt
 * or transmit . return number of packets we tried to send
 */

static int mpi_send_packet (struct net_device *dev)
{
	struct sk_buff *skb;
	unsigned char *buffer;
	s16 len;
	__le16 *payloadLen;
	struct airo_info *ai = dev->ml_priv;
	u8 *sendbuf;

	/* get a packet to send */

	if ((skb = skb_dequeue(&ai->txq)) == NULL) {
		airo_print_err(dev->name,
			"%s: Dequeue'd zero in send_packet()",
			__func__);
		return 0;
	}

	/* check min length*/
	len = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	buffer = skb->data;

	ai->txfids[0].tx_desc.offset = 0;
	ai->txfids[0].tx_desc.valid = 1;
	ai->txfids[0].tx_desc.eoc = 1;
	ai->txfids[0].tx_desc.len =len+sizeof(WifiHdr);

/*
 * Magic, the cards firmware needs a length count (2 bytes) in the host buffer
 * right after  TXFID_HDR.The TXFID_HDR contains the status short so payloadlen
 * is immediately after it. ------------------------------------------------
 *                         |TXFIDHDR+STATUS|PAYLOADLEN|802.3HDR|PACKETDATA|
 *                         ------------------------------------------------
 */

	memcpy(ai->txfids[0].virtual_host_addr,
		(char *)&wifictlhdr8023, sizeof(wifictlhdr8023));

	payloadLen = (__le16 *)(ai->txfids[0].virtual_host_addr +
		sizeof(wifictlhdr8023));
	sendbuf = ai->txfids[0].virtual_host_addr +
		sizeof(wifictlhdr8023) + 2 ;

	/*
	 * Firmware automatically puts 802 header on so
	 * we don't need to account for it in the length
	 */
	if (test_bit(FLAG_MIC_CAPABLE, &ai->flags) && ai->micstats.enabled &&
		(ntohs(((__be16 *)buffer)[6]) != 0x888E)) {
		MICBuffer pMic;

		if (encapsulate(ai, (etherHead *)buffer, &pMic, len - sizeof(etherHead)) != SUCCESS)
			return ERROR;

		*payloadLen = cpu_to_le16(len-sizeof(etherHead)+sizeof(pMic));
		ai->txfids[0].tx_desc.len += sizeof(pMic);
		/* copy data into airo dma buffer */
		memcpy (sendbuf, buffer, sizeof(etherHead));
		buffer += sizeof(etherHead);
		sendbuf += sizeof(etherHead);
		memcpy (sendbuf, &pMic, sizeof(pMic));
		sendbuf += sizeof(pMic);
		memcpy (sendbuf, buffer, len - sizeof(etherHead));
	} else {
		*payloadLen = cpu_to_le16(len - sizeof(etherHead));

		netif_trans_update(dev);

		/* copy data into airo dma buffer */
		memcpy(sendbuf, buffer, len);
	}

	memcpy_toio(ai->txfids[0].card_ram_off,
		&ai->txfids[0].tx_desc, sizeof(TxFid));

	OUT4500(ai, EVACK, 8);

	dev_kfree_skb_any(skb);
	return 1;
}

static void get_tx_error(struct airo_info *ai, s32 fid)
{
	__le16 status;

	if (fid < 0)
		status = ((WifiCtlHdr *)ai->txfids[0].virtual_host_addr)->ctlhdr.status;
	else {
		if (bap_setup(ai, ai->fids[fid] & 0xffff, 4, BAP0) != SUCCESS)
			return;
		bap_read(ai, &status, 2, BAP0);
	}
	if (le16_to_cpu(status) & 2) /* Too many retries */
		ai->dev->stats.tx_aborted_errors++;
	if (le16_to_cpu(status) & 4) /* Transmit lifetime exceeded */
		ai->dev->stats.tx_heartbeat_errors++;
	if (le16_to_cpu(status) & 8) /* Aid fail */
		{ }
	if (le16_to_cpu(status) & 0x10) /* MAC disabled */
		ai->dev->stats.tx_carrier_errors++;
	if (le16_to_cpu(status) & 0x20) /* Association lost */
		{ }
	/* We produce a TXDROP event only for retry or lifetime
	 * exceeded, because that's the only status that really mean
	 * that this particular node went away.
	 * Other errors means that *we* screwed up. - Jean II */
	if ((le16_to_cpu(status) & 2) ||
	     (le16_to_cpu(status) & 4)) {
		union iwreq_data	wrqu;
		char junk[0x18];

		/* Faster to skip over useless data than to do
		 * another bap_setup(). We are at offset 0x6 and
		 * need to go to 0x18 and read 6 bytes - Jean II */
		bap_read(ai, (__le16 *) junk, 0x18, BAP0);

		/* Copy 802.11 dest address.
		 * We use the 802.11 header because the frame may
		 * not be 802.3 or may be mangled...
		 * In Ad-Hoc mode, it will be the node address.
		 * In managed mode, it will be most likely the AP addr
		 * User space will figure out how to convert it to
		 * whatever it needs (IP address or else).
		 * - Jean II */
		memcpy(wrqu.addr.sa_data, junk + 0x12, ETH_ALEN);
		wrqu.addr.sa_family = ARPHRD_ETHER;

		/* Send event to user space */
		wireless_send_event(ai->dev, IWEVTXDROP, &wrqu, NULL);
	}
}

static void airo_end_xmit(struct net_device *dev) {
	u16 status;
	int i;
	struct airo_info *priv = dev->ml_priv;
	struct sk_buff *skb = priv->xmit.skb;
	int fid = priv->xmit.fid;
	u32 *fids = priv->fids;

	clear_bit(JOB_XMIT, &priv->jobs);
	clear_bit(FLAG_PENDING_XMIT, &priv->flags);
	status = transmit_802_3_packet (priv, fids[fid], skb->data);
	up(&priv->sem);

	i = 0;
	if ( status == SUCCESS ) {
		netif_trans_update(dev);
		for (; i < MAX_FIDS / 2 && (priv->fids[i] & 0xffff0000); i++);
	} else {
		priv->fids[fid] &= 0xffff;
		dev->stats.tx_window_errors++;
	}
	if (i < MAX_FIDS / 2)
		netif_wake_queue(dev);
	dev_kfree_skb(skb);
}

static netdev_tx_t airo_start_xmit(struct sk_buff *skb,
					 struct net_device *dev)
{
	s16 len;
	int i, j;
	struct airo_info *priv = dev->ml_priv;
	u32 *fids = priv->fids;

	if ( skb == NULL ) {
		airo_print_err(dev->name, "%s: skb == NULL!", __func__);
		return NETDEV_TX_OK;
	}

	/* Find a vacant FID */
	for( i = 0; i < MAX_FIDS / 2 && (fids[i] & 0xffff0000); i++ );
	for( j = i + 1; j < MAX_FIDS / 2 && (fids[j] & 0xffff0000); j++ );

	if ( j >= MAX_FIDS / 2 ) {
		netif_stop_queue(dev);

		if (i == MAX_FIDS / 2) {
			dev->stats.tx_fifo_errors++;
			return NETDEV_TX_BUSY;
		}
	}
	/* check min length*/
	len = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
        /* Mark fid as used & save length for later */
	fids[i] |= (len << 16);
	priv->xmit.skb = skb;
	priv->xmit.fid = i;
	if (down_trylock(&priv->sem) != 0) {
		set_bit(FLAG_PENDING_XMIT, &priv->flags);
		netif_stop_queue(dev);
		set_bit(JOB_XMIT, &priv->jobs);
		wake_up_interruptible(&priv->thr_wait);
	} else
		airo_end_xmit(dev);
	return NETDEV_TX_OK;
}

static void airo_end_xmit11(struct net_device *dev) {
	u16 status;
	int i;
	struct airo_info *priv = dev->ml_priv;
	struct sk_buff *skb = priv->xmit11.skb;
	int fid = priv->xmit11.fid;
	u32 *fids = priv->fids;

	clear_bit(JOB_XMIT11, &priv->jobs);
	clear_bit(FLAG_PENDING_XMIT11, &priv->flags);
	status = transmit_802_11_packet (priv, fids[fid], skb->data);
	up(&priv->sem);

	i = MAX_FIDS / 2;
	if ( status == SUCCESS ) {
		netif_trans_update(dev);
		for (; i < MAX_FIDS && (priv->fids[i] & 0xffff0000); i++);
	} else {
		priv->fids[fid] &= 0xffff;
		dev->stats.tx_window_errors++;
	}
	if (i < MAX_FIDS)
		netif_wake_queue(dev);
	dev_kfree_skb(skb);
}

static netdev_tx_t airo_start_xmit11(struct sk_buff *skb,
					   struct net_device *dev)
{
	s16 len;
	int i, j;
	struct airo_info *priv = dev->ml_priv;
	u32 *fids = priv->fids;

	if (test_bit(FLAG_MPI, &priv->flags)) {
		/* Not implemented yet for MPI350 */
		netif_stop_queue(dev);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if ( skb == NULL ) {
		airo_print_err(dev->name, "%s: skb == NULL!", __func__);
		return NETDEV_TX_OK;
	}

	/* Find a vacant FID */
	for( i = MAX_FIDS / 2; i < MAX_FIDS && (fids[i] & 0xffff0000); i++ );
	for( j = i + 1; j < MAX_FIDS && (fids[j] & 0xffff0000); j++ );

	if ( j >= MAX_FIDS ) {
		netif_stop_queue(dev);

		if (i == MAX_FIDS) {
			dev->stats.tx_fifo_errors++;
			return NETDEV_TX_BUSY;
		}
	}
	/* check min length*/
	len = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
        /* Mark fid as used & save length for later */
	fids[i] |= (len << 16);
	priv->xmit11.skb = skb;
	priv->xmit11.fid = i;
	if (down_trylock(&priv->sem) != 0) {
		set_bit(FLAG_PENDING_XMIT11, &priv->flags);
		netif_stop_queue(dev);
		set_bit(JOB_XMIT11, &priv->jobs);
		wake_up_interruptible(&priv->thr_wait);
	} else
		airo_end_xmit11(dev);
	return NETDEV_TX_OK;
}

static void airo_read_stats(struct net_device *dev)
{
	struct airo_info *ai = dev->ml_priv;
	StatsRid stats_rid;
	__le32 *vals = stats_rid.vals;

	clear_bit(JOB_STATS, &ai->jobs);
	if (ai->power.event) {
		up(&ai->sem);
		return;
	}
	readStatsRid(ai, &stats_rid, RID_STATS, 0);
	up(&ai->sem);

	dev->stats.rx_packets = le32_to_cpu(vals[43]) + le32_to_cpu(vals[44]) +
			       le32_to_cpu(vals[45]);
	dev->stats.tx_packets = le32_to_cpu(vals[39]) + le32_to_cpu(vals[40]) +
			       le32_to_cpu(vals[41]);
	dev->stats.rx_bytes = le32_to_cpu(vals[92]);
	dev->stats.tx_bytes = le32_to_cpu(vals[91]);
	dev->stats.rx_errors = le32_to_cpu(vals[0]) + le32_to_cpu(vals[2]) +
			      le32_to_cpu(vals[3]) + le32_to_cpu(vals[4]);
	dev->stats.tx_errors = le32_to_cpu(vals[42]) +
			      dev->stats.tx_fifo_errors;
	dev->stats.multicast = le32_to_cpu(vals[43]);
	dev->stats.collisions = le32_to_cpu(vals[89]);

	/* detailed rx_errors: */
	dev->stats.rx_length_errors = le32_to_cpu(vals[3]);
	dev->stats.rx_crc_errors = le32_to_cpu(vals[4]);
	dev->stats.rx_frame_errors = le32_to_cpu(vals[2]);
	dev->stats.rx_fifo_errors = le32_to_cpu(vals[0]);
}

static struct net_device_stats *airo_get_stats(struct net_device *dev)
{
	struct airo_info *local =  dev->ml_priv;

	if (!test_bit(JOB_STATS, &local->jobs)) {
		/* Get stats out of the card if available */
		if (down_trylock(&local->sem) != 0) {
			set_bit(JOB_STATS, &local->jobs);
			wake_up_interruptible(&local->thr_wait);
		} else
			airo_read_stats(dev);
	}

	return &dev->stats;
}

static void airo_set_promisc(struct airo_info *ai) {
	Cmd cmd;
	Resp rsp;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd=CMD_SETMODE;
	clear_bit(JOB_PROMISC, &ai->jobs);
	cmd.parm0=(ai->flags&IFF_PROMISC) ? PROMISC : NOPROMISC;
	issuecommand(ai, &cmd, &rsp);
	up(&ai->sem);
}

static void airo_set_multicast_list(struct net_device *dev) {
	struct airo_info *ai = dev->ml_priv;

	if ((dev->flags ^ ai->flags) & IFF_PROMISC) {
		change_bit(FLAG_PROMISC, &ai->flags);
		if (down_trylock(&ai->sem) != 0) {
			set_bit(JOB_PROMISC, &ai->jobs);
			wake_up_interruptible(&ai->thr_wait);
		} else
			airo_set_promisc(ai);
	}

	if ((dev->flags&IFF_ALLMULTI) || !netdev_mc_empty(dev)) {
		/* Turn on multicast.  (Should be already setup...) */
	}
}

static int airo_set_mac_address(struct net_device *dev, void *p)
{
	struct airo_info *ai = dev->ml_priv;
	struct sockaddr *addr = p;

	readConfigRid(ai, 1);
	memcpy (ai->config.macAddr, addr->sa_data, dev->addr_len);
	set_bit (FLAG_COMMIT, &ai->flags);
	disable_MAC(ai, 1);
	writeConfigRid (ai, 1);
	enable_MAC(ai, 1);
	memcpy (ai->dev->dev_addr, addr->sa_data, dev->addr_len);
	if (ai->wifidev)
		memcpy (ai->wifidev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}

static LIST_HEAD(airo_devices);

static void add_airo_dev(struct airo_info *ai)
{
	/* Upper layers already keep track of PCI devices,
	 * so we only need to remember our non-PCI cards. */
	if (!ai->pci)
		list_add_tail(&ai->dev_list, &airo_devices);
}

static void del_airo_dev(struct airo_info *ai)
{
	if (!ai->pci)
		list_del(&ai->dev_list);
}

static int airo_close(struct net_device *dev) {
	struct airo_info *ai = dev->ml_priv;

	netif_stop_queue(dev);

	if (ai->wifidev != dev) {
#ifdef POWER_ON_DOWN
		/* Shut power to the card. The idea is that the user can save
		 * power when he doesn't need the card with "ifconfig down".
		 * That's the method that is most friendly towards the network
		 * stack (i.e. the network stack won't try to broadcast
		 * anything on the interface and routes are gone. Jean II */
		set_bit(FLAG_RADIO_DOWN, &ai->flags);
		disable_MAC(ai, 1);
#endif
		disable_interrupts( ai );

		free_irq(dev->irq, dev);

		set_bit(JOB_DIE, &ai->jobs);
		kthread_stop(ai->airo_thread_task);
	}
	return 0;
}

void stop_airo_card( struct net_device *dev, int freeres )
{
	struct airo_info *ai = dev->ml_priv;

	set_bit(FLAG_RADIO_DOWN, &ai->flags);
	disable_MAC(ai, 1);
	disable_interrupts(ai);
	takedown_proc_entry( dev, ai );
	if (test_bit(FLAG_REGISTERED, &ai->flags)) {
		unregister_netdev( dev );
		if (ai->wifidev) {
			unregister_netdev(ai->wifidev);
			free_netdev(ai->wifidev);
			ai->wifidev = NULL;
		}
		clear_bit(FLAG_REGISTERED, &ai->flags);
	}
	/*
	 * Clean out tx queue
	 */
	if (test_bit(FLAG_MPI, &ai->flags) && !skb_queue_empty(&ai->txq)) {
		struct sk_buff *skb = NULL;
		for (;(skb = skb_dequeue(&ai->txq));)
			dev_kfree_skb(skb);
	}

	airo_networks_free (ai);

	kfree(ai->flash);
	kfree(ai->rssi);
	kfree(ai->SSID);
	if (freeres) {
		/* PCMCIA frees this stuff, so only for PCI and ISA */
	        release_region( dev->base_addr, 64 );
		if (test_bit(FLAG_MPI, &ai->flags)) {
			if (ai->pci)
				mpi_unmap_card(ai->pci);
			if (ai->pcimem)
				iounmap(ai->pcimem);
			if (ai->pciaux)
				iounmap(ai->pciaux);
			pci_free_consistent(ai->pci, PCI_SHARED_LEN,
				ai->shared, ai->shared_dma);
		}
        }
	crypto_free_sync_skcipher(ai->tfm);
	del_airo_dev(ai);
	free_netdev( dev );
}

EXPORT_SYMBOL(stop_airo_card);

static int wll_header_parse(const struct sk_buff *skb, unsigned char *haddr)
{
	memcpy(haddr, skb_mac_header(skb) + 10, ETH_ALEN);
	return ETH_ALEN;
}

static void mpi_unmap_card(struct pci_dev *pci)
{
	unsigned long mem_start = pci_resource_start(pci, 1);
	unsigned long mem_len = pci_resource_len(pci, 1);
	unsigned long aux_start = pci_resource_start(pci, 2);
	unsigned long aux_len = AUXMEMSIZE;

	release_mem_region(aux_start, aux_len);
	release_mem_region(mem_start, mem_len);
}

/*************************************************************
 *  This routine assumes that descriptors have been setup .
 *  Run at insmod time or after reset  when the decriptors
 *  have been initialized . Returns 0 if all is well nz
 *  otherwise . Does not allocate memory but sets up card
 *  using previously allocated descriptors.
 */
static int mpi_init_descriptors (struct airo_info *ai)
{
	Cmd cmd;
	Resp rsp;
	int i;
	int rc = SUCCESS;

	/* Alloc  card RX descriptors */
	netif_stop_queue(ai->dev);

	memset(&rsp,0,sizeof(rsp));
	memset(&cmd,0,sizeof(cmd));

	cmd.cmd = CMD_ALLOCATEAUX;
	cmd.parm0 = FID_RX;
	cmd.parm1 = (ai->rxfids[0].card_ram_off - ai->pciaux);
	cmd.parm2 = MPI_MAX_FIDS;
	rc=issuecommand(ai, &cmd, &rsp);
	if (rc != SUCCESS) {
		airo_print_err(ai->dev->name, "Couldn't allocate RX FID");
		return rc;
	}

	for (i=0; i<MPI_MAX_FIDS; i++) {
		memcpy_toio(ai->rxfids[i].card_ram_off,
			&ai->rxfids[i].rx_desc, sizeof(RxFid));
	}

	/* Alloc card TX descriptors */

	memset(&rsp,0,sizeof(rsp));
	memset(&cmd,0,sizeof(cmd));

	cmd.cmd = CMD_ALLOCATEAUX;
	cmd.parm0 = FID_TX;
	cmd.parm1 = (ai->txfids[0].card_ram_off - ai->pciaux);
	cmd.parm2 = MPI_MAX_FIDS;

	for (i=0; i<MPI_MAX_FIDS; i++) {
		ai->txfids[i].tx_desc.valid = 1;
		memcpy_toio(ai->txfids[i].card_ram_off,
			&ai->txfids[i].tx_desc, sizeof(TxFid));
	}
	ai->txfids[i-1].tx_desc.eoc = 1; /* Last descriptor has EOC set */

	rc=issuecommand(ai, &cmd, &rsp);
	if (rc != SUCCESS) {
		airo_print_err(ai->dev->name, "Couldn't allocate TX FID");
		return rc;
	}

	/* Alloc card Rid descriptor */
	memset(&rsp,0,sizeof(rsp));
	memset(&cmd,0,sizeof(cmd));

	cmd.cmd = CMD_ALLOCATEAUX;
	cmd.parm0 = RID_RW;
	cmd.parm1 = (ai->config_desc.card_ram_off - ai->pciaux);
	cmd.parm2 = 1; /* Magic number... */
	rc=issuecommand(ai, &cmd, &rsp);
	if (rc != SUCCESS) {
		airo_print_err(ai->dev->name, "Couldn't allocate RID");
		return rc;
	}

	memcpy_toio(ai->config_desc.card_ram_off,
		&ai->config_desc.rid_desc, sizeof(Rid));

	return rc;
}

/*
 * We are setting up three things here:
 * 1) Map AUX memory for descriptors: Rid, TxFid, or RxFid.
 * 2) Map PCI memory for issuing commands.
 * 3) Allocate memory (shared) to send and receive ethernet frames.
 */
static int mpi_map_card(struct airo_info *ai, struct pci_dev *pci)
{
	unsigned long mem_start, mem_len, aux_start, aux_len;
	int rc = -1;
	int i;
	dma_addr_t busaddroff;
	unsigned char *vpackoff;
	unsigned char __iomem *pciaddroff;

	mem_start = pci_resource_start(pci, 1);
	mem_len = pci_resource_len(pci, 1);
	aux_start = pci_resource_start(pci, 2);
	aux_len = AUXMEMSIZE;

	if (!request_mem_region(mem_start, mem_len, DRV_NAME)) {
		airo_print_err("", "Couldn't get region %x[%x]",
			(int)mem_start, (int)mem_len);
		goto out;
	}
	if (!request_mem_region(aux_start, aux_len, DRV_NAME)) {
		airo_print_err("", "Couldn't get region %x[%x]",
			(int)aux_start, (int)aux_len);
		goto free_region1;
	}

	ai->pcimem = ioremap(mem_start, mem_len);
	if (!ai->pcimem) {
		airo_print_err("", "Couldn't map region %x[%x]",
			(int)mem_start, (int)mem_len);
		goto free_region2;
	}
	ai->pciaux = ioremap(aux_start, aux_len);
	if (!ai->pciaux) {
		airo_print_err("", "Couldn't map region %x[%x]",
			(int)aux_start, (int)aux_len);
		goto free_memmap;
	}

	/* Reserve PKTSIZE for each fid and 2K for the Rids */
	ai->shared = pci_alloc_consistent(pci, PCI_SHARED_LEN, &ai->shared_dma);
	if (!ai->shared) {
		airo_print_err("", "Couldn't alloc_consistent %d",
			PCI_SHARED_LEN);
		goto free_auxmap;
	}

	/*
	 * Setup descriptor RX, TX, CONFIG
	 */
	busaddroff = ai->shared_dma;
	pciaddroff = ai->pciaux + AUX_OFFSET;
	vpackoff   = ai->shared;

	/* RX descriptor setup */
	for(i = 0; i < MPI_MAX_FIDS; i++) {
		ai->rxfids[i].pending = 0;
		ai->rxfids[i].card_ram_off = pciaddroff;
		ai->rxfids[i].virtual_host_addr = vpackoff;
		ai->rxfids[i].rx_desc.host_addr = busaddroff;
		ai->rxfids[i].rx_desc.valid = 1;
		ai->rxfids[i].rx_desc.len = PKTSIZE;
		ai->rxfids[i].rx_desc.rdy = 0;

		pciaddroff += sizeof(RxFid);
		busaddroff += PKTSIZE;
		vpackoff   += PKTSIZE;
	}

	/* TX descriptor setup */
	for(i = 0; i < MPI_MAX_FIDS; i++) {
		ai->txfids[i].card_ram_off = pciaddroff;
		ai->txfids[i].virtual_host_addr = vpackoff;
		ai->txfids[i].tx_desc.valid = 1;
		ai->txfids[i].tx_desc.host_addr = busaddroff;
		memcpy(ai->txfids[i].virtual_host_addr,
			&wifictlhdr8023, sizeof(wifictlhdr8023));

		pciaddroff += sizeof(TxFid);
		busaddroff += PKTSIZE;
		vpackoff   += PKTSIZE;
	}
	ai->txfids[i-1].tx_desc.eoc = 1; /* Last descriptor has EOC set */

	/* Rid descriptor setup */
	ai->config_desc.card_ram_off = pciaddroff;
	ai->config_desc.virtual_host_addr = vpackoff;
	ai->config_desc.rid_desc.host_addr = busaddroff;
	ai->ridbus = busaddroff;
	ai->config_desc.rid_desc.rid = 0;
	ai->config_desc.rid_desc.len = RIDSIZE;
	ai->config_desc.rid_desc.valid = 1;
	pciaddroff += sizeof(Rid);
	busaddroff += RIDSIZE;
	vpackoff   += RIDSIZE;

	/* Tell card about descriptors */
	if (mpi_init_descriptors (ai) != SUCCESS)
		goto free_shared;

	return 0;
 free_shared:
	pci_free_consistent(pci, PCI_SHARED_LEN, ai->shared, ai->shared_dma);
 free_auxmap:
	iounmap(ai->pciaux);
 free_memmap:
	iounmap(ai->pcimem);
 free_region2:
	release_mem_region(aux_start, aux_len);
 free_region1:
	release_mem_region(mem_start, mem_len);
 out:
	return rc;
}

static const struct header_ops airo_header_ops = {
	.parse = wll_header_parse,
};

static const struct net_device_ops airo11_netdev_ops = {
	.ndo_open 		= airo_open,
	.ndo_stop 		= airo_close,
	.ndo_start_xmit 	= airo_start_xmit11,
	.ndo_get_stats 		= airo_get_stats,
	.ndo_set_mac_address	= airo_set_mac_address,
	.ndo_do_ioctl		= airo_ioctl,
};

static void wifi_setup(struct net_device *dev)
{
	dev->netdev_ops = &airo11_netdev_ops;
	dev->header_ops = &airo_header_ops;
	dev->wireless_handlers = &airo_handler_def;

	dev->type               = ARPHRD_IEEE80211;
	dev->hard_header_len    = ETH_HLEN;
	dev->mtu                = AIRO_DEF_MTU;
	dev->min_mtu            = 68;
	dev->max_mtu            = MIC_MSGLEN_MAX;
	dev->addr_len           = ETH_ALEN;
	dev->tx_queue_len       = 100; 

	eth_broadcast_addr(dev->broadcast);

	dev->flags              = IFF_BROADCAST|IFF_MULTICAST;
}

static struct net_device *init_wifidev(struct airo_info *ai,
					struct net_device *ethdev)
{
	int err;
	struct net_device *dev = alloc_netdev(0, "wifi%d", NET_NAME_UNKNOWN,
					      wifi_setup);
	if (!dev)
		return NULL;
	dev->ml_priv = ethdev->ml_priv;
	dev->irq = ethdev->irq;
	dev->base_addr = ethdev->base_addr;
	dev->wireless_data = ethdev->wireless_data;
	SET_NETDEV_DEV(dev, ethdev->dev.parent);
	eth_hw_addr_inherit(dev, ethdev);
	err = register_netdev(dev);
	if (err<0) {
		free_netdev(dev);
		return NULL;
	}
	return dev;
}

static int reset_card( struct net_device *dev , int lock) {
	struct airo_info *ai = dev->ml_priv;

	if (lock && down_interruptible(&ai->sem))
		return -1;
	waitbusy (ai);
	OUT4500(ai,COMMAND,CMD_SOFTRESET);
	msleep(200);
	waitbusy (ai);
	msleep(200);
	if (lock)
		up(&ai->sem);
	return 0;
}

#define AIRO_MAX_NETWORK_COUNT	64
static int airo_networks_allocate(struct airo_info *ai)
{
	if (ai->networks)
		return 0;

	ai->networks = kcalloc(AIRO_MAX_NETWORK_COUNT, sizeof(BSSListElement),
			       GFP_KERNEL);
	if (!ai->networks) {
		airo_print_warn("", "Out of memory allocating beacons");
		return -ENOMEM;
	}

	return 0;
}

static void airo_networks_free(struct airo_info *ai)
{
	kfree(ai->networks);
	ai->networks = NULL;
}

static void airo_networks_initialize(struct airo_info *ai)
{
	int i;

	INIT_LIST_HEAD(&ai->network_free_list);
	INIT_LIST_HEAD(&ai->network_list);
	for (i = 0; i < AIRO_MAX_NETWORK_COUNT; i++)
		list_add_tail(&ai->networks[i].list,
			      &ai->network_free_list);
}

static const struct net_device_ops airo_netdev_ops = {
	.ndo_open		= airo_open,
	.ndo_stop		= airo_close,
	.ndo_start_xmit		= airo_start_xmit,
	.ndo_get_stats		= airo_get_stats,
	.ndo_set_rx_mode	= airo_set_multicast_list,
	.ndo_set_mac_address	= airo_set_mac_address,
	.ndo_do_ioctl		= airo_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
};

static const struct net_device_ops mpi_netdev_ops = {
	.ndo_open		= airo_open,
	.ndo_stop		= airo_close,
	.ndo_start_xmit		= mpi_start_xmit,
	.ndo_get_stats		= airo_get_stats,
	.ndo_set_rx_mode	= airo_set_multicast_list,
	.ndo_set_mac_address	= airo_set_mac_address,
	.ndo_do_ioctl		= airo_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
};


static struct net_device *_init_airo_card( unsigned short irq, int port,
					   int is_pcmcia, struct pci_dev *pci,
					   struct device *dmdev )
{
	struct net_device *dev;
	struct airo_info *ai;
	int i, rc;
	CapabilityRid cap_rid;

	/* Create the network device object. */
	dev = alloc_netdev(sizeof(*ai), "", NET_NAME_UNKNOWN, ether_setup);
	if (!dev) {
		airo_print_err("", "Couldn't alloc_etherdev");
		return NULL;
	}

	ai = dev->ml_priv = netdev_priv(dev);
	ai->wifidev = NULL;
	ai->flags = 1 << FLAG_RADIO_DOWN;
	ai->jobs = 0;
	ai->dev = dev;
	if (pci && (pci->device == 0x5000 || pci->device == 0xa504)) {
		airo_print_dbg("", "Found an MPI350 card");
		set_bit(FLAG_MPI, &ai->flags);
	}
	spin_lock_init(&ai->aux_lock);
	sema_init(&ai->sem, 1);
	ai->config.len = 0;
	ai->pci = pci;
	init_waitqueue_head (&ai->thr_wait);
	ai->tfm = NULL;
	add_airo_dev(ai);
	ai->APList.len = cpu_to_le16(sizeof(struct APListRid));

	if (airo_networks_allocate (ai))
		goto err_out_free;
	airo_networks_initialize (ai);

	skb_queue_head_init (&ai->txq);

	/* The Airo-specific entries in the device structure. */
	if (test_bit(FLAG_MPI,&ai->flags))
		dev->netdev_ops = &mpi_netdev_ops;
	else
		dev->netdev_ops = &airo_netdev_ops;
	dev->wireless_handlers = &airo_handler_def;
	ai->wireless_data.spy_data = &ai->spy_data;
	dev->wireless_data = &ai->wireless_data;
	dev->irq = irq;
	dev->base_addr = port;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->max_mtu = MIC_MSGLEN_MAX;

	SET_NETDEV_DEV(dev, dmdev);

	reset_card (dev, 1);
	msleep(400);

	if (!is_pcmcia) {
		if (!request_region(dev->base_addr, 64, DRV_NAME)) {
			rc = -EBUSY;
			airo_print_err(dev->name, "Couldn't request region");
			goto err_out_nets;
		}
	}

	if (test_bit(FLAG_MPI,&ai->flags)) {
		if (mpi_map_card(ai, pci)) {
			airo_print_err("", "Could not map memory");
			goto err_out_res;
		}
	}

	if (probe) {
		if (setup_card(ai, dev->dev_addr, 1) != SUCCESS) {
			airo_print_err(dev->name, "MAC could not be enabled" );
			rc = -EIO;
			goto err_out_map;
		}
	} else if (!test_bit(FLAG_MPI,&ai->flags)) {
		ai->bap_read = fast_bap_read;
		set_bit(FLAG_FLASHING, &ai->flags);
	}

	strcpy(dev->name, "eth%d");
	rc = register_netdev(dev);
	if (rc) {
		airo_print_err(dev->name, "Couldn't register_netdev");
		goto err_out_map;
	}
	ai->wifidev = init_wifidev(ai, dev);
	if (!ai->wifidev)
		goto err_out_reg;

	rc = readCapabilityRid(ai, &cap_rid, 1);
	if (rc != SUCCESS) {
		rc = -EIO;
		goto err_out_wifi;
	}
	/* WEP capability discovery */
	ai->wep_capable = (cap_rid.softCap & cpu_to_le16(0x02)) ? 1 : 0;
	ai->max_wep_idx = (cap_rid.softCap & cpu_to_le16(0x80)) ? 3 : 0;

	airo_print_info(dev->name, "Firmware version %x.%x.%02d",
	                ((le16_to_cpu(cap_rid.softVer) >> 8) & 0xF),
	                (le16_to_cpu(cap_rid.softVer) & 0xFF),
	                le16_to_cpu(cap_rid.softSubVer));

	/* Test for WPA support */
	/* Only firmware versions 5.30.17 or better can do WPA */
	if (le16_to_cpu(cap_rid.softVer) > 0x530
	 || (le16_to_cpu(cap_rid.softVer) == 0x530
	      && le16_to_cpu(cap_rid.softSubVer) >= 17)) {
		airo_print_info(ai->dev->name, "WPA supported.");

		set_bit(FLAG_WPA_CAPABLE, &ai->flags);
		ai->bssListFirst = RID_WPA_BSSLISTFIRST;
		ai->bssListNext = RID_WPA_BSSLISTNEXT;
		ai->bssListRidLen = sizeof(BSSListRid);
	} else {
		airo_print_info(ai->dev->name, "WPA unsupported with firmware "
			"versions older than 5.30.17.");

		ai->bssListFirst = RID_BSSLISTFIRST;
		ai->bssListNext = RID_BSSLISTNEXT;
		ai->bssListRidLen = sizeof(BSSListRid) - sizeof(BSSListRidExtra);
	}

	set_bit(FLAG_REGISTERED,&ai->flags);
	airo_print_info(dev->name, "MAC enabled %pM", dev->dev_addr);

	/* Allocate the transmit buffers */
	if (probe && !test_bit(FLAG_MPI,&ai->flags))
		for( i = 0; i < MAX_FIDS; i++ )
			ai->fids[i] = transmit_allocate(ai,AIRO_DEF_MTU,i>=MAX_FIDS/2);

	if (setup_proc_entry(dev, dev->ml_priv) < 0)
		goto err_out_wifi;

	return dev;

err_out_wifi:
	unregister_netdev(ai->wifidev);
	free_netdev(ai->wifidev);
err_out_reg:
	unregister_netdev(dev);
err_out_map:
	if (test_bit(FLAG_MPI,&ai->flags) && pci) {
		pci_free_consistent(pci, PCI_SHARED_LEN, ai->shared, ai->shared_dma);
		iounmap(ai->pciaux);
		iounmap(ai->pcimem);
		mpi_unmap_card(ai->pci);
	}
err_out_res:
	if (!is_pcmcia)
	        release_region( dev->base_addr, 64 );
err_out_nets:
	airo_networks_free(ai);
err_out_free:
	del_airo_dev(ai);
	free_netdev(dev);
	return NULL;
}

struct net_device *init_airo_card( unsigned short irq, int port, int is_pcmcia,
				  struct device *dmdev)
{
	return _init_airo_card ( irq, port, is_pcmcia, NULL, dmdev);
}

EXPORT_SYMBOL(init_airo_card);

static int waitbusy (struct airo_info *ai) {
	int delay = 0;
	while ((IN4500(ai, COMMAND) & COMMAND_BUSY) && (delay < 10000)) {
		udelay (10);
		if ((++delay % 20) == 0)
			OUT4500(ai, EVACK, EV_CLEARCOMMANDBUSY);
	}
	return delay < 10000;
}

int reset_airo_card( struct net_device *dev )
{
	int i;
	struct airo_info *ai = dev->ml_priv;

	if (reset_card (dev, 1))
		return -1;

	if ( setup_card(ai, dev->dev_addr, 1 ) != SUCCESS ) {
		airo_print_err(dev->name, "MAC could not be enabled");
		return -1;
	}
	airo_print_info(dev->name, "MAC enabled %pM", dev->dev_addr);
	/* Allocate the transmit buffers if needed */
	if (!test_bit(FLAG_MPI,&ai->flags))
		for( i = 0; i < MAX_FIDS; i++ )
			ai->fids[i] = transmit_allocate (ai,AIRO_DEF_MTU,i>=MAX_FIDS/2);

	enable_interrupts( ai );
	netif_wake_queue(dev);
	return 0;
}

EXPORT_SYMBOL(reset_airo_card);

static void airo_send_event(struct net_device *dev) {
	struct airo_info *ai = dev->ml_priv;
	union iwreq_data wrqu;
	StatusRid status_rid;

	clear_bit(JOB_EVENT, &ai->jobs);
	PC4500_readrid(ai, RID_STATUS, &status_rid, sizeof(status_rid), 0);
	up(&ai->sem);
	wrqu.data.length = 0;
	wrqu.data.flags = 0;
	memcpy(wrqu.ap_addr.sa_data, status_rid.bssid[0], ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	/* Send event to user space */
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
}

static void airo_process_scan_results (struct airo_info *ai) {
	union iwreq_data	wrqu;
	BSSListRid bss;
	int rc;
	BSSListElement * loop_net;
	BSSListElement * tmp_net;

	/* Blow away current list of scan results */
	list_for_each_entry_safe (loop_net, tmp_net, &ai->network_list, list) {
		list_move_tail (&loop_net->list, &ai->network_free_list);
		/* Don't blow away ->list, just BSS data */
		memset (loop_net, 0, sizeof (loop_net->bss));
	}

	/* Try to read the first entry of the scan result */
	rc = PC4500_readrid(ai, ai->bssListFirst, &bss, ai->bssListRidLen, 0);
	if((rc) || (bss.index == cpu_to_le16(0xffff))) {
		/* No scan results */
		goto out;
	}

	/* Read and parse all entries */
	tmp_net = NULL;
	while((!rc) && (bss.index != cpu_to_le16(0xffff))) {
		/* Grab a network off the free list */
		if (!list_empty(&ai->network_free_list)) {
			tmp_net = list_entry(ai->network_free_list.next,
					    BSSListElement, list);
			list_del(ai->network_free_list.next);
		}

		if (tmp_net != NULL) {
			memcpy(tmp_net, &bss, sizeof(tmp_net->bss));
			list_add_tail(&tmp_net->list, &ai->network_list);
			tmp_net = NULL;
		}

		/* Read next entry */
		rc = PC4500_readrid(ai, ai->bssListNext,
				    &bss, ai->bssListRidLen, 0);
	}

out:
	/* write APList back (we cleared it in airo_set_scan) */
	disable_MAC(ai, 2);
	writeAPListRid(ai, &ai->APList, 0);
	enable_MAC(ai, 0);

	ai->scan_timeout = 0;
	clear_bit(JOB_SCAN_RESULTS, &ai->jobs);
	up(&ai->sem);

	/* Send an empty event to user space.
	 * We don't send the received data on
	 * the event because it would require
	 * us to do complex transcoding, and
	 * we want to minimise the work done in
	 * the irq handler. Use a request to
	 * extract the data - Jean II */
	wrqu.data.length = 0;
	wrqu.data.flags = 0;
	wireless_send_event(ai->dev, SIOCGIWSCAN, &wrqu, NULL);
}

static int airo_thread(void *data) {
	struct net_device *dev = data;
	struct airo_info *ai = dev->ml_priv;
	int locked;

	set_freezable();
	while(1) {
		/* make swsusp happy with our thread */
		try_to_freeze();

		if (test_bit(JOB_DIE, &ai->jobs))
			break;

		if (ai->jobs) {
			locked = down_interruptible(&ai->sem);
		} else {
			wait_queue_entry_t wait;

			init_waitqueue_entry(&wait, current);
			add_wait_queue(&ai->thr_wait, &wait);
			for (;;) {
				set_current_state(TASK_INTERRUPTIBLE);
				if (ai->jobs)
					break;
				if (ai->expires || ai->scan_timeout) {
					if (ai->scan_timeout &&
							time_after_eq(jiffies,ai->scan_timeout)){
						set_bit(JOB_SCAN_RESULTS, &ai->jobs);
						break;
					} else if (ai->expires &&
							time_after_eq(jiffies,ai->expires)){
						set_bit(JOB_AUTOWEP, &ai->jobs);
						break;
					}
					if (!kthread_should_stop() &&
					    !freezing(current)) {
						unsigned long wake_at;
						if (!ai->expires || !ai->scan_timeout) {
							wake_at = max(ai->expires,
								ai->scan_timeout);
						} else {
							wake_at = min(ai->expires,
								ai->scan_timeout);
						}
						schedule_timeout(wake_at - jiffies);
						continue;
					}
				} else if (!kthread_should_stop() &&
					   !freezing(current)) {
					schedule();
					continue;
				}
				break;
			}
			current->state = TASK_RUNNING;
			remove_wait_queue(&ai->thr_wait, &wait);
			locked = 1;
		}

		if (locked)
			continue;

		if (test_bit(JOB_DIE, &ai->jobs)) {
			up(&ai->sem);
			break;
		}

		if (ai->power.event || test_bit(FLAG_FLASHING, &ai->flags)) {
			up(&ai->sem);
			continue;
		}

		if (test_bit(JOB_XMIT, &ai->jobs))
			airo_end_xmit(dev);
		else if (test_bit(JOB_XMIT11, &ai->jobs))
			airo_end_xmit11(dev);
		else if (test_bit(JOB_STATS, &ai->jobs))
			airo_read_stats(dev);
		else if (test_bit(JOB_WSTATS, &ai->jobs))
			airo_read_wireless_stats(ai);
		else if (test_bit(JOB_PROMISC, &ai->jobs))
			airo_set_promisc(ai);
		else if (test_bit(JOB_MIC, &ai->jobs))
			micinit(ai);
		else if (test_bit(JOB_EVENT, &ai->jobs))
			airo_send_event(dev);
		else if (test_bit(JOB_AUTOWEP, &ai->jobs))
			timer_func(dev);
		else if (test_bit(JOB_SCAN_RESULTS, &ai->jobs))
			airo_process_scan_results(ai);
		else  /* Shouldn't get here, but we make sure to unlock */
			up(&ai->sem);
	}

	return 0;
}

static int header_len(__le16 ctl)
{
	u16 fc = le16_to_cpu(ctl);
	switch (fc & 0xc) {
	case 4:
		if ((fc & 0xe0) == 0xc0)
			return 10;	/* one-address control packet */
		return 16;	/* two-address control packet */
	case 8:
		if ((fc & 0x300) == 0x300)
			return 30;	/* WDS packet */
	}
	return 24;
}

static void airo_handle_cisco_mic(struct airo_info *ai)
{
	if (test_bit(FLAG_MIC_CAPABLE, &ai->flags)) {
		set_bit(JOB_MIC, &ai->jobs);
		wake_up_interruptible(&ai->thr_wait);
	}
}

/* Airo Status codes */
#define STAT_NOBEACON	0x8000 /* Loss of sync - missed beacons */
#define STAT_MAXRETRIES	0x8001 /* Loss of sync - max retries */
#define STAT_MAXARL	0x8002 /* Loss of sync - average retry level exceeded*/
#define STAT_FORCELOSS	0x8003 /* Loss of sync - host request */
#define STAT_TSFSYNC	0x8004 /* Loss of sync - TSF synchronization */
#define STAT_DEAUTH	0x8100 /* low byte is 802.11 reason code */
#define STAT_DISASSOC	0x8200 /* low byte is 802.11 reason code */
#define STAT_ASSOC_FAIL	0x8400 /* low byte is 802.11 reason code */
#define STAT_AUTH_FAIL	0x0300 /* low byte is 802.11 reason code */
#define STAT_ASSOC	0x0400 /* Associated */
#define STAT_REASSOC    0x0600 /* Reassociated?  Only on firmware >= 5.30.17 */

static void airo_print_status(const char *devname, u16 status)
{
	u8 reason = status & 0xFF;

	switch (status & 0xFF00) {
	case STAT_NOBEACON:
		switch (status) {
		case STAT_NOBEACON:
			airo_print_dbg(devname, "link lost (missed beacons)");
			break;
		case STAT_MAXRETRIES:
		case STAT_MAXARL:
			airo_print_dbg(devname, "link lost (max retries)");
			break;
		case STAT_FORCELOSS:
			airo_print_dbg(devname, "link lost (local choice)");
			break;
		case STAT_TSFSYNC:
			airo_print_dbg(devname, "link lost (TSF sync lost)");
			break;
		default:
			airo_print_dbg(devname, "unknown status %x\n", status);
			break;
		}
		break;
	case STAT_DEAUTH:
		airo_print_dbg(devname, "deauthenticated (reason: %d)", reason);
		break;
	case STAT_DISASSOC:
		airo_print_dbg(devname, "disassociated (reason: %d)", reason);
		break;
	case STAT_ASSOC_FAIL:
		airo_print_dbg(devname, "association failed (reason: %d)",
			       reason);
		break;
	case STAT_AUTH_FAIL:
		airo_print_dbg(devname, "authentication failed (reason: %d)",
			       reason);
		break;
	case STAT_ASSOC:
	case STAT_REASSOC:
		break;
	default:
		airo_print_dbg(devname, "unknown status %x\n", status);
		break;
	}
}

static void airo_handle_link(struct airo_info *ai)
{
	union iwreq_data wrqu;
	int scan_forceloss = 0;
	u16 status;

	/* Get new status and acknowledge the link change */
	status = le16_to_cpu(IN4500(ai, LINKSTAT));
	OUT4500(ai, EVACK, EV_LINK);

	if ((status == STAT_FORCELOSS) && (ai->scan_timeout > 0))
		scan_forceloss = 1;

	airo_print_status(ai->dev->name, status);

	if ((status == STAT_ASSOC) || (status == STAT_REASSOC)) {
		if (auto_wep)
			ai->expires = 0;
		if (ai->list_bss_task)
			wake_up_process(ai->list_bss_task);
		set_bit(FLAG_UPDATE_UNI, &ai->flags);
		set_bit(FLAG_UPDATE_MULTI, &ai->flags);

		if (down_trylock(&ai->sem) != 0) {
			set_bit(JOB_EVENT, &ai->jobs);
			wake_up_interruptible(&ai->thr_wait);
		} else
			airo_send_event(ai->dev);
		netif_carrier_on(ai->dev);
	} else if (!scan_forceloss) {
		if (auto_wep && !ai->expires) {
			ai->expires = RUN_AT(3*HZ);
			wake_up_interruptible(&ai->thr_wait);
		}

		/* Send event to user space */
		eth_zero_addr(wrqu.ap_addr.sa_data);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(ai->dev, SIOCGIWAP, &wrqu, NULL);
		netif_carrier_off(ai->dev);
	} else {
		netif_carrier_off(ai->dev);
	}
}

static void airo_handle_rx(struct airo_info *ai)
{
	struct sk_buff *skb = NULL;
	__le16 fc, v, *buffer, tmpbuf[4];
	u16 len, hdrlen = 0, gap, fid;
	struct rx_hdr hdr;
	int success = 0;

	if (test_bit(FLAG_MPI, &ai->flags)) {
		if (test_bit(FLAG_802_11, &ai->flags))
			mpi_receive_802_11(ai);
		else
			mpi_receive_802_3(ai);
		OUT4500(ai, EVACK, EV_RX);
		return;
	}

	fid = IN4500(ai, RXFID);

	/* Get the packet length */
	if (test_bit(FLAG_802_11, &ai->flags)) {
		bap_setup (ai, fid, 4, BAP0);
		bap_read (ai, (__le16*)&hdr, sizeof(hdr), BAP0);
		/* Bad CRC. Ignore packet */
		if (le16_to_cpu(hdr.status) & 2)
			hdr.len = 0;
		if (ai->wifidev == NULL)
			hdr.len = 0;
	} else {
		bap_setup(ai, fid, 0x36, BAP0);
		bap_read(ai, &hdr.len, 2, BAP0);
	}
	len = le16_to_cpu(hdr.len);

	if (len > AIRO_DEF_MTU) {
		airo_print_err(ai->dev->name, "Bad size %d", len);
		goto done;
	}
	if (len == 0)
		goto done;

	if (test_bit(FLAG_802_11, &ai->flags)) {
		bap_read(ai, &fc, sizeof (fc), BAP0);
		hdrlen = header_len(fc);
	} else
		hdrlen = ETH_ALEN * 2;

	skb = dev_alloc_skb(len + hdrlen + 2 + 2);
	if (!skb) {
		ai->dev->stats.rx_dropped++;
		goto done;
	}

	skb_reserve(skb, 2); /* This way the IP header is aligned */
	buffer = skb_put(skb, len + hdrlen);
	if (test_bit(FLAG_802_11, &ai->flags)) {
		buffer[0] = fc;
		bap_read(ai, buffer + 1, hdrlen - 2, BAP0);
		if (hdrlen == 24)
			bap_read(ai, tmpbuf, 6, BAP0);

		bap_read(ai, &v, sizeof(v), BAP0);
		gap = le16_to_cpu(v);
		if (gap) {
			if (gap <= 8) {
				bap_read(ai, tmpbuf, gap, BAP0);
			} else {
				airo_print_err(ai->dev->name, "gaplen too "
					"big. Problems will follow...");
			}
		}
		bap_read(ai, buffer + hdrlen/2, len, BAP0);
	} else {
		MICBuffer micbuf;

		bap_read(ai, buffer, ETH_ALEN * 2, BAP0);
		if (ai->micstats.enabled) {
			bap_read(ai, (__le16 *) &micbuf, sizeof (micbuf), BAP0);
			if (ntohs(micbuf.typelen) > 0x05DC)
				bap_setup(ai, fid, 0x44, BAP0);
			else {
				if (len <= sizeof (micbuf)) {
					dev_kfree_skb_irq(skb);
					goto done;
				}

				len -= sizeof(micbuf);
				skb_trim(skb, len + hdrlen);
			}
		}

		bap_read(ai, buffer + ETH_ALEN, len, BAP0);
		if (decapsulate(ai, &micbuf, (etherHead*) buffer, len))
			dev_kfree_skb_irq (skb);
		else
			success = 1;
	}

#ifdef WIRELESS_SPY
	if (success && (ai->spy_data.spy_number > 0)) {
		char *sa;
		struct iw_quality wstats;

		/* Prepare spy data : addr + qual */
		if (!test_bit(FLAG_802_11, &ai->flags)) {
			sa = (char *) buffer + 6;
			bap_setup(ai, fid, 8, BAP0);
			bap_read(ai, (__le16 *) hdr.rssi, 2, BAP0);
		} else
			sa = (char *) buffer + 10;
		wstats.qual = hdr.rssi[0];
		if (ai->rssi)
			wstats.level = 0x100 - ai->rssi[hdr.rssi[1]].rssidBm;
		else
			wstats.level = (hdr.rssi[1] + 321) / 2;
		wstats.noise = ai->wstats.qual.noise;
		wstats.updated =  IW_QUAL_LEVEL_UPDATED
				| IW_QUAL_QUAL_UPDATED
				| IW_QUAL_DBM;
		/* Update spy records */
		wireless_spy_update(ai->dev, sa, &wstats);
	}
#endif /* WIRELESS_SPY */

done:
	OUT4500(ai, EVACK, EV_RX);

	if (success) {
		if (test_bit(FLAG_802_11, &ai->flags)) {
			skb_reset_mac_header(skb);
			skb->pkt_type = PACKET_OTHERHOST;
			skb->dev = ai->wifidev;
			skb->protocol = htons(ETH_P_802_2);
		} else
			skb->protocol = eth_type_trans(skb, ai->dev);
		skb->ip_summed = CHECKSUM_NONE;

		netif_rx(skb);
	}
}

static void airo_handle_tx(struct airo_info *ai, u16 status)
{
	int i, index = -1;
	u16 fid;

	if (test_bit(FLAG_MPI, &ai->flags)) {
		unsigned long flags;

		if (status & EV_TXEXC)
			get_tx_error(ai, -1);

		spin_lock_irqsave(&ai->aux_lock, flags);
		if (!skb_queue_empty(&ai->txq)) {
			spin_unlock_irqrestore(&ai->aux_lock,flags);
			mpi_send_packet(ai->dev);
		} else {
			clear_bit(FLAG_PENDING_XMIT, &ai->flags);
			spin_unlock_irqrestore(&ai->aux_lock,flags);
			netif_wake_queue(ai->dev);
		}
		OUT4500(ai, EVACK, status & (EV_TX | EV_TXCPY | EV_TXEXC));
		return;
	}

	fid = IN4500(ai, TXCOMPLFID);

	for (i = 0; i < MAX_FIDS; i++) {
		if ((ai->fids[i] & 0xffff) == fid)
			index = i;
	}

	if (index != -1) {
		if (status & EV_TXEXC)
			get_tx_error(ai, index);

		OUT4500(ai, EVACK, status & (EV_TX | EV_TXEXC));

		/* Set up to be used again */
		ai->fids[index] &= 0xffff;
		if (index < MAX_FIDS / 2) {
			if (!test_bit(FLAG_PENDING_XMIT, &ai->flags))
				netif_wake_queue(ai->dev);
		} else {
			if (!test_bit(FLAG_PENDING_XMIT11, &ai->flags))
				netif_wake_queue(ai->wifidev);
		}
	} else {
		OUT4500(ai, EVACK, status & (EV_TX | EV_TXCPY | EV_TXEXC));
		airo_print_err(ai->dev->name, "Unallocated FID was used to xmit");
	}
}

static irqreturn_t airo_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	u16 status, savedInterrupts = 0;
	struct airo_info *ai = dev->ml_priv;
	int handled = 0;

	if (!netif_device_present(dev))
		return IRQ_NONE;

	for (;;) {
		status = IN4500(ai, EVSTAT);
		if (!(status & STATUS_INTS) || (status == 0xffff))
			break;

		handled = 1;

		if (status & EV_AWAKE) {
			OUT4500(ai, EVACK, EV_AWAKE);
			OUT4500(ai, EVACK, EV_AWAKE);
		}

		if (!savedInterrupts) {
			savedInterrupts = IN4500(ai, EVINTEN);
			OUT4500(ai, EVINTEN, 0);
		}

		if (status & EV_MIC) {
			OUT4500(ai, EVACK, EV_MIC);
			airo_handle_cisco_mic(ai);
		}

		if (status & EV_LINK) {
			/* Link status changed */
			airo_handle_link(ai);
		}

		/* Check to see if there is something to receive */
		if (status & EV_RX)
			airo_handle_rx(ai);

		/* Check to see if a packet has been transmitted */
		if (status & (EV_TX | EV_TXCPY | EV_TXEXC))
			airo_handle_tx(ai, status);

		if ( status & ~STATUS_INTS & ~IGNORE_INTS ) {
			airo_print_warn(ai->dev->name, "Got weird status %x",
				status & ~STATUS_INTS & ~IGNORE_INTS );
		}
	}

	if (savedInterrupts)
		OUT4500(ai, EVINTEN, savedInterrupts);

	return IRQ_RETVAL(handled);
}

/*
 *  Routines to talk to the card
 */

/*
 *  This was originally written for the 4500, hence the name
 *  NOTE:  If use with 8bit mode and SMP bad things will happen!
 *         Why would some one do 8 bit IO in an SMP machine?!?
 */
static void OUT4500( struct airo_info *ai, u16 reg, u16 val ) {
	if (test_bit(FLAG_MPI,&ai->flags))
		reg <<= 1;
	if ( !do8bitIO )
		outw( val, ai->dev->base_addr + reg );
	else {
		outb( val & 0xff, ai->dev->base_addr + reg );
		outb( val >> 8, ai->dev->base_addr + reg + 1 );
	}
}

static u16 IN4500( struct airo_info *ai, u16 reg ) {
	unsigned short rc;

	if (test_bit(FLAG_MPI,&ai->flags))
		reg <<= 1;
	if ( !do8bitIO )
		rc = inw( ai->dev->base_addr + reg );
	else {
		rc = inb( ai->dev->base_addr + reg );
		rc += ((int)inb( ai->dev->base_addr + reg + 1 )) << 8;
	}
	return rc;
}

static int enable_MAC(struct airo_info *ai, int lock)
{
	int rc;
	Cmd cmd;
	Resp rsp;

	/* FLAG_RADIO_OFF : Radio disabled via /proc or Wireless Extensions
	 * FLAG_RADIO_DOWN : Radio disabled via "ifconfig ethX down"
	 * Note : we could try to use !netif_running(dev) in enable_MAC()
	 * instead of this flag, but I don't trust it *within* the
	 * open/close functions, and testing both flags together is
	 * "cheaper" - Jean II */
	if (ai->flags & FLAG_RADIO_MASK) return SUCCESS;

	if (lock && down_interruptible(&ai->sem))
		return -ERESTARTSYS;

	if (!test_bit(FLAG_ENABLED, &ai->flags)) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd = MAC_ENABLE;
		rc = issuecommand(ai, &cmd, &rsp);
		if (rc == SUCCESS)
			set_bit(FLAG_ENABLED, &ai->flags);
	} else
		rc = SUCCESS;

	if (lock)
	    up(&ai->sem);

	if (rc)
		airo_print_err(ai->dev->name, "Cannot enable MAC");
	else if ((rsp.status & 0xFF00) != 0) {
		airo_print_err(ai->dev->name, "Bad MAC enable reason=%x, "
			"rid=%x, offset=%d", rsp.rsp0, rsp.rsp1, rsp.rsp2);
		rc = ERROR;
	}
	return rc;
}

static void disable_MAC( struct airo_info *ai, int lock ) {
        Cmd cmd;
	Resp rsp;

	if (lock == 1 && down_interruptible(&ai->sem))
		return;

	if (test_bit(FLAG_ENABLED, &ai->flags)) {
		if (lock != 2) /* lock == 2 means don't disable carrier */
			netif_carrier_off(ai->dev);
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd = MAC_DISABLE; // disable in case already enabled
		issuecommand(ai, &cmd, &rsp);
		clear_bit(FLAG_ENABLED, &ai->flags);
	}
	if (lock == 1)
		up(&ai->sem);
}

static void enable_interrupts( struct airo_info *ai ) {
	/* Enable the interrupts */
	OUT4500( ai, EVINTEN, STATUS_INTS );
}

static void disable_interrupts( struct airo_info *ai ) {
	OUT4500( ai, EVINTEN, 0 );
}

static void mpi_receive_802_3(struct airo_info *ai)
{
	RxFid rxd;
	int len = 0;
	struct sk_buff *skb;
	char *buffer;
	int off = 0;
	MICBuffer micbuf;

	memcpy_fromio(&rxd, ai->rxfids[0].card_ram_off, sizeof(rxd));
	/* Make sure we got something */
	if (rxd.rdy && rxd.valid == 0) {
		len = rxd.len + 12;
		if (len < 12 || len > 2048)
			goto badrx;

		skb = dev_alloc_skb(len);
		if (!skb) {
			ai->dev->stats.rx_dropped++;
			goto badrx;
		}
		buffer = skb_put(skb,len);
		memcpy(buffer, ai->rxfids[0].virtual_host_addr, ETH_ALEN * 2);
		if (ai->micstats.enabled) {
			memcpy(&micbuf,
				ai->rxfids[0].virtual_host_addr + ETH_ALEN * 2,
				sizeof(micbuf));
			if (ntohs(micbuf.typelen) <= 0x05DC) {
				if (len <= sizeof(micbuf) + ETH_ALEN * 2)
					goto badmic;

				off = sizeof(micbuf);
				skb_trim (skb, len - off);
			}
		}
		memcpy(buffer + ETH_ALEN * 2,
			ai->rxfids[0].virtual_host_addr + ETH_ALEN * 2 + off,
			len - ETH_ALEN * 2 - off);
		if (decapsulate (ai, &micbuf, (etherHead*)buffer, len - off - ETH_ALEN * 2)) {
badmic:
			dev_kfree_skb_irq (skb);
			goto badrx;
		}
#ifdef WIRELESS_SPY
		if (ai->spy_data.spy_number > 0) {
			char *sa;
			struct iw_quality wstats;
			/* Prepare spy data : addr + qual */
			sa = buffer + ETH_ALEN;
			wstats.qual = 0; /* XXX Where do I get that info from ??? */
			wstats.level = 0;
			wstats.updated = 0;
			/* Update spy records */
			wireless_spy_update(ai->dev, sa, &wstats);
		}
#endif /* WIRELESS_SPY */

		skb->ip_summed = CHECKSUM_NONE;
		skb->protocol = eth_type_trans(skb, ai->dev);
		netif_rx(skb);
	}
badrx:
	if (rxd.valid == 0) {
		rxd.valid = 1;
		rxd.rdy = 0;
		rxd.len = PKTSIZE;
		memcpy_toio(ai->rxfids[0].card_ram_off, &rxd, sizeof(rxd));
	}
}

static void mpi_receive_802_11(struct airo_info *ai)
{
	RxFid rxd;
	struct sk_buff *skb = NULL;
	u16 len, hdrlen = 0;
	__le16 fc;
	struct rx_hdr hdr;
	u16 gap;
	u16 *buffer;
	char *ptr = ai->rxfids[0].virtual_host_addr + 4;

	memcpy_fromio(&rxd, ai->rxfids[0].card_ram_off, sizeof(rxd));
	memcpy ((char *)&hdr, ptr, sizeof(hdr));
	ptr += sizeof(hdr);
	/* Bad CRC. Ignore packet */
	if (le16_to_cpu(hdr.status) & 2)
		hdr.len = 0;
	if (ai->wifidev == NULL)
		hdr.len = 0;
	len = le16_to_cpu(hdr.len);
	if (len > AIRO_DEF_MTU) {
		airo_print_err(ai->dev->name, "Bad size %d", len);
		goto badrx;
	}
	if (len == 0)
		goto badrx;

	fc = get_unaligned((__le16 *)ptr);
	hdrlen = header_len(fc);

	skb = dev_alloc_skb( len + hdrlen + 2 );
	if ( !skb ) {
		ai->dev->stats.rx_dropped++;
		goto badrx;
	}
	buffer = skb_put(skb, len + hdrlen);
	memcpy ((char *)buffer, ptr, hdrlen);
	ptr += hdrlen;
	if (hdrlen == 24)
		ptr += 6;
	gap = get_unaligned_le16(ptr);
	ptr += sizeof(__le16);
	if (gap) {
		if (gap <= 8)
			ptr += gap;
		else
			airo_print_err(ai->dev->name,
			    "gaplen too big. Problems will follow...");
	}
	memcpy ((char *)buffer + hdrlen, ptr, len);
	ptr += len;
#ifdef IW_WIRELESS_SPY	  /* defined in iw_handler.h */
	if (ai->spy_data.spy_number > 0) {
		char *sa;
		struct iw_quality wstats;
		/* Prepare spy data : addr + qual */
		sa = (char*)buffer + 10;
		wstats.qual = hdr.rssi[0];
		if (ai->rssi)
			wstats.level = 0x100 - ai->rssi[hdr.rssi[1]].rssidBm;
		else
			wstats.level = (hdr.rssi[1] + 321) / 2;
		wstats.noise = ai->wstats.qual.noise;
		wstats.updated = IW_QUAL_QUAL_UPDATED
			| IW_QUAL_LEVEL_UPDATED
			| IW_QUAL_DBM;
		/* Update spy records */
		wireless_spy_update(ai->dev, sa, &wstats);
	}
#endif /* IW_WIRELESS_SPY */
	skb_reset_mac_header(skb);
	skb->pkt_type = PACKET_OTHERHOST;
	skb->dev = ai->wifidev;
	skb->protocol = htons(ETH_P_802_2);
	skb->ip_summed = CHECKSUM_NONE;
	netif_rx( skb );

badrx:
	if (rxd.valid == 0) {
		rxd.valid = 1;
		rxd.rdy = 0;
		rxd.len = PKTSIZE;
		memcpy_toio(ai->rxfids[0].card_ram_off, &rxd, sizeof(rxd));
	}
}

static inline void set_auth_type(struct airo_info *local, int auth_type)
{
	local->config.authType = auth_type;
	/* Cache the last auth type used (of AUTH_OPEN and AUTH_ENCRYPT).
	 * Used by airo_set_auth()
	 */
	if (auth_type == AUTH_OPEN || auth_type == AUTH_ENCRYPT)
		local->last_auth = auth_type;
}

static u16 setup_card(struct airo_info *ai, u8 *mac, int lock)
{
	Cmd cmd;
	Resp rsp;
	int status;
	SsidRid mySsid;
	__le16 lastindex;
	WepKeyRid wkr;
	int rc;

	memset( &mySsid, 0, sizeof( mySsid ) );
	kfree (ai->flash);
	ai->flash = NULL;

	/* The NOP is the first step in getting the card going */
	cmd.cmd = NOP;
	cmd.parm0 = cmd.parm1 = cmd.parm2 = 0;
	if (lock && down_interruptible(&ai->sem))
		return ERROR;
	if ( issuecommand( ai, &cmd, &rsp ) != SUCCESS ) {
		if (lock)
			up(&ai->sem);
		return ERROR;
	}
	disable_MAC( ai, 0);

	// Let's figure out if we need to use the AUX port
	if (!test_bit(FLAG_MPI,&ai->flags)) {
		cmd.cmd = CMD_ENABLEAUX;
		if (issuecommand(ai, &cmd, &rsp) != SUCCESS) {
			if (lock)
				up(&ai->sem);
			airo_print_err(ai->dev->name, "Error checking for AUX port");
			return ERROR;
		}
		if (!aux_bap || rsp.status & 0xff00) {
			ai->bap_read = fast_bap_read;
			airo_print_dbg(ai->dev->name, "Doing fast bap_reads");
		} else {
			ai->bap_read = aux_bap_read;
			airo_print_dbg(ai->dev->name, "Doing AUX bap_reads");
		}
	}
	if (lock)
		up(&ai->sem);
	if (ai->config.len == 0) {
		int i;
		tdsRssiRid rssi_rid;
		CapabilityRid cap_rid;

		kfree(ai->SSID);
		ai->SSID = NULL;
		// general configuration (read/modify/write)
		status = readConfigRid(ai, lock);
		if ( status != SUCCESS ) return ERROR;

		status = readCapabilityRid(ai, &cap_rid, lock);
		if ( status != SUCCESS ) return ERROR;

		status = PC4500_readrid(ai,RID_RSSI,&rssi_rid,sizeof(rssi_rid),lock);
		if ( status == SUCCESS ) {
			if (ai->rssi || (ai->rssi = kmalloc(512, GFP_KERNEL)) != NULL)
				memcpy(ai->rssi, (u8*)&rssi_rid + 2, 512); /* Skip RID length member */
		}
		else {
			kfree(ai->rssi);
			ai->rssi = NULL;
			if (cap_rid.softCap & cpu_to_le16(8))
				ai->config.rmode |= RXMODE_NORMALIZED_RSSI;
			else
				airo_print_warn(ai->dev->name, "unknown received signal "
						"level scale");
		}
		ai->config.opmode = adhoc ? MODE_STA_IBSS : MODE_STA_ESS;
		set_auth_type(ai, AUTH_OPEN);
		ai->config.modulation = MOD_CCK;

		if (le16_to_cpu(cap_rid.len) >= sizeof(cap_rid) &&
		    (cap_rid.extSoftCap & cpu_to_le16(1)) &&
		    micsetup(ai) == SUCCESS) {
			ai->config.opmode |= MODE_MIC;
			set_bit(FLAG_MIC_CAPABLE, &ai->flags);
		}

		/* Save off the MAC */
		for( i = 0; i < ETH_ALEN; i++ ) {
			mac[i] = ai->config.macAddr[i];
		}

		/* Check to see if there are any insmod configured
		   rates to add */
		if ( rates[0] ) {
			memset(ai->config.rates,0,sizeof(ai->config.rates));
			for( i = 0; i < 8 && rates[i]; i++ ) {
				ai->config.rates[i] = rates[i];
			}
		}
		set_bit (FLAG_COMMIT, &ai->flags);
	}

	/* Setup the SSIDs if present */
	if ( ssids[0] ) {
		int i;
		for( i = 0; i < 3 && ssids[i]; i++ ) {
			size_t len = strlen(ssids[i]);
			if (len > 32)
				len = 32;
			mySsid.ssids[i].len = cpu_to_le16(len);
			memcpy(mySsid.ssids[i].ssid, ssids[i], len);
		}
		mySsid.len = cpu_to_le16(sizeof(mySsid));
	}

	status = writeConfigRid(ai, lock);
	if ( status != SUCCESS ) return ERROR;

	/* Set up the SSID list */
	if ( ssids[0] ) {
		status = writeSsidRid(ai, &mySsid, lock);
		if ( status != SUCCESS ) return ERROR;
	}

	status = enable_MAC(ai, lock);
	if (status != SUCCESS)
		return ERROR;

	/* Grab the initial wep key, we gotta save it for auto_wep */
	rc = readWepKeyRid(ai, &wkr, 1, lock);
	if (rc == SUCCESS) do {
		lastindex = wkr.kindex;
		if (wkr.kindex == cpu_to_le16(0xffff)) {
			ai->defindex = wkr.mac[0];
		}
		rc = readWepKeyRid(ai, &wkr, 0, lock);
	} while(lastindex != wkr.kindex);

	try_auto_wep(ai);

	return SUCCESS;
}

static u16 issuecommand(struct airo_info *ai, Cmd *pCmd, Resp *pRsp) {
        // Im really paranoid about letting it run forever!
	int max_tries = 600000;

	if (IN4500(ai, EVSTAT) & EV_CMD)
		OUT4500(ai, EVACK, EV_CMD);

	OUT4500(ai, PARAM0, pCmd->parm0);
	OUT4500(ai, PARAM1, pCmd->parm1);
	OUT4500(ai, PARAM2, pCmd->parm2);
	OUT4500(ai, COMMAND, pCmd->cmd);

	while (max_tries-- && (IN4500(ai, EVSTAT) & EV_CMD) == 0) {
		if ((IN4500(ai, COMMAND)) == pCmd->cmd)
			// PC4500 didn't notice command, try again
			OUT4500(ai, COMMAND, pCmd->cmd);
		if (!in_atomic() && (max_tries & 255) == 0)
			schedule();
	}

	if ( max_tries == -1 ) {
		airo_print_err(ai->dev->name,
			"Max tries exceeded when issuing command");
		if (IN4500(ai, COMMAND) & COMMAND_BUSY)
			OUT4500(ai, EVACK, EV_CLEARCOMMANDBUSY);
		return ERROR;
	}

	// command completed
	pRsp->status = IN4500(ai, STATUS);
	pRsp->rsp0 = IN4500(ai, RESP0);
	pRsp->rsp1 = IN4500(ai, RESP1);
	pRsp->rsp2 = IN4500(ai, RESP2);
	if ((pRsp->status & 0xff00)!=0 && pCmd->cmd != CMD_SOFTRESET)
		airo_print_err(ai->dev->name,
			"cmd:%x status:%x rsp0:%x rsp1:%x rsp2:%x",
			pCmd->cmd, pRsp->status, pRsp->rsp0, pRsp->rsp1,
			pRsp->rsp2);

	// clear stuck command busy if necessary
	if (IN4500(ai, COMMAND) & COMMAND_BUSY) {
		OUT4500(ai, EVACK, EV_CLEARCOMMANDBUSY);
	}
	// acknowledge processing the status/response
	OUT4500(ai, EVACK, EV_CMD);

	return SUCCESS;
}

/* Sets up the bap to start exchange data.  whichbap should
 * be one of the BAP0 or BAP1 defines.  Locks should be held before
 * calling! */
static int bap_setup(struct airo_info *ai, u16 rid, u16 offset, int whichbap )
{
	int timeout = 50;
	int max_tries = 3;

	OUT4500(ai, SELECT0+whichbap, rid);
	OUT4500(ai, OFFSET0+whichbap, offset);
	while (1) {
		int status = IN4500(ai, OFFSET0+whichbap);
		if (status & BAP_BUSY) {
                        /* This isn't really a timeout, but its kinda
			   close */
			if (timeout--) {
				continue;
			}
		} else if ( status & BAP_ERR ) {
			/* invalid rid or offset */
			airo_print_err(ai->dev->name, "BAP error %x %d",
				status, whichbap );
			return ERROR;
		} else if (status & BAP_DONE) { // success
			return SUCCESS;
		}
		if ( !(max_tries--) ) {
			airo_print_err(ai->dev->name,
				"BAP setup error too many retries\n");
			return ERROR;
		}
		// -- PC4500 missed it, try again
		OUT4500(ai, SELECT0+whichbap, rid);
		OUT4500(ai, OFFSET0+whichbap, offset);
		timeout = 50;
	}
}

/* should only be called by aux_bap_read.  This aux function and the
   following use concepts not documented in the developers guide.  I
   got them from a patch given to my by Aironet */
static u16 aux_setup(struct airo_info *ai, u16 page,
		     u16 offset, u16 *len)
{
	u16 next;

	OUT4500(ai, AUXPAGE, page);
	OUT4500(ai, AUXOFF, 0);
	next = IN4500(ai, AUXDATA);
	*len = IN4500(ai, AUXDATA)&0xff;
	if (offset != 4) OUT4500(ai, AUXOFF, offset);
	return next;
}

/* requires call to bap_setup() first */
static int aux_bap_read(struct airo_info *ai, __le16 *pu16Dst,
			int bytelen, int whichbap)
{
	u16 len;
	u16 page;
	u16 offset;
	u16 next;
	int words;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ai->aux_lock, flags);
	page = IN4500(ai, SWS0+whichbap);
	offset = IN4500(ai, SWS2+whichbap);
	next = aux_setup(ai, page, offset, &len);
	words = (bytelen+1)>>1;

	for (i=0; i<words;) {
		int count;
		count = (len>>1) < (words-i) ? (len>>1) : (words-i);
		if ( !do8bitIO )
			insw( ai->dev->base_addr+DATA0+whichbap,
			      pu16Dst+i,count );
		else
			insb( ai->dev->base_addr+DATA0+whichbap,
			      pu16Dst+i, count << 1 );
		i += count;
		if (i<words) {
			next = aux_setup(ai, next, 4, &len);
		}
	}
	spin_unlock_irqrestore(&ai->aux_lock, flags);
	return SUCCESS;
}


/* requires call to bap_setup() first */
static int fast_bap_read(struct airo_info *ai, __le16 *pu16Dst,
			 int bytelen, int whichbap)
{
	bytelen = (bytelen + 1) & (~1); // round up to even value
	if ( !do8bitIO )
		insw( ai->dev->base_addr+DATA0+whichbap, pu16Dst, bytelen>>1 );
	else
		insb( ai->dev->base_addr+DATA0+whichbap, pu16Dst, bytelen );
	return SUCCESS;
}

/* requires call to bap_setup() first */
static int bap_write(struct airo_info *ai, const __le16 *pu16Src,
		     int bytelen, int whichbap)
{
	bytelen = (bytelen + 1) & (~1); // round up to even value
	if ( !do8bitIO )
		outsw( ai->dev->base_addr+DATA0+whichbap,
		       pu16Src, bytelen>>1 );
	else
		outsb( ai->dev->base_addr+DATA0+whichbap, pu16Src, bytelen );
	return SUCCESS;
}

static int PC4500_accessrid(struct airo_info *ai, u16 rid, u16 accmd)
{
	Cmd cmd; /* for issuing commands */
	Resp rsp; /* response from commands */
	u16 status;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = accmd;
	cmd.parm0 = rid;
	status = issuecommand(ai, &cmd, &rsp);
	if (status != 0) return status;
	if ( (rsp.status & 0x7F00) != 0) {
		return (accmd << 8) + (rsp.rsp0 & 0xFF);
	}
	return 0;
}

/*  Note, that we are using BAP1 which is also used by transmit, so
 *  we must get a lock. */
static int PC4500_readrid(struct airo_info *ai, u16 rid, void *pBuf, int len, int lock)
{
	u16 status;
        int rc = SUCCESS;

	if (lock) {
		if (down_interruptible(&ai->sem))
			return ERROR;
	}
	if (test_bit(FLAG_MPI,&ai->flags)) {
		Cmd cmd;
		Resp rsp;

		memset(&cmd, 0, sizeof(cmd));
		memset(&rsp, 0, sizeof(rsp));
		ai->config_desc.rid_desc.valid = 1;
		ai->config_desc.rid_desc.len = RIDSIZE;
		ai->config_desc.rid_desc.rid = 0;
		ai->config_desc.rid_desc.host_addr = ai->ridbus;

		cmd.cmd = CMD_ACCESS;
		cmd.parm0 = rid;

		memcpy_toio(ai->config_desc.card_ram_off,
			&ai->config_desc.rid_desc, sizeof(Rid));

		rc = issuecommand(ai, &cmd, &rsp);

		if (rsp.status & 0x7f00)
			rc = rsp.rsp0;
		if (!rc)
			memcpy(pBuf, ai->config_desc.virtual_host_addr, len);
		goto done;
	} else {
		if ((status = PC4500_accessrid(ai, rid, CMD_ACCESS))!=SUCCESS) {
	                rc = status;
	                goto done;
	        }
		if (bap_setup(ai, rid, 0, BAP1) != SUCCESS) {
			rc = ERROR;
	                goto done;
	        }
		// read the rid length field
		bap_read(ai, pBuf, 2, BAP1);
		// length for remaining part of rid
		len = min(len, (int)le16_to_cpu(*(__le16*)pBuf)) - 2;

		if ( len <= 2 ) {
			airo_print_err(ai->dev->name,
				"Rid %x has a length of %d which is too short",
				(int)rid, (int)len );
			rc = ERROR;
	                goto done;
		}
		// read remainder of the rid
		rc = bap_read(ai, ((__le16*)pBuf)+1, len, BAP1);
	}
done:
	if (lock)
		up(&ai->sem);
	return rc;
}

/*  Note, that we are using BAP1 which is also used by transmit, so
 *  make sure this isn't called when a transmit is happening */
static int PC4500_writerid(struct airo_info *ai, u16 rid,
			   const void *pBuf, int len, int lock)
{
	u16 status;
	int rc = SUCCESS;

	*(__le16*)pBuf = cpu_to_le16((u16)len);

	if (lock) {
		if (down_interruptible(&ai->sem))
			return ERROR;
	}
	if (test_bit(FLAG_MPI,&ai->flags)) {
		Cmd cmd;
		Resp rsp;

		if (test_bit(FLAG_ENABLED, &ai->flags) && (RID_WEP_TEMP != rid))
			airo_print_err(ai->dev->name,
				"%s: MAC should be disabled (rid=%04x)",
				__func__, rid);
		memset(&cmd, 0, sizeof(cmd));
		memset(&rsp, 0, sizeof(rsp));

		ai->config_desc.rid_desc.valid = 1;
		ai->config_desc.rid_desc.len = *((u16 *)pBuf);
		ai->config_desc.rid_desc.rid = 0;

		cmd.cmd = CMD_WRITERID;
		cmd.parm0 = rid;

		memcpy_toio(ai->config_desc.card_ram_off,
			&ai->config_desc.rid_desc, sizeof(Rid));

		if (len < 4 || len > 2047) {
			airo_print_err(ai->dev->name, "%s: len=%d", __func__, len);
			rc = -1;
		} else {
			memcpy(ai->config_desc.virtual_host_addr,
				pBuf, len);

			rc = issuecommand(ai, &cmd, &rsp);
			if ((rc & 0xff00) != 0) {
				airo_print_err(ai->dev->name, "%s: Write rid Error %d",
						__func__, rc);
				airo_print_err(ai->dev->name, "%s: Cmd=%04x",
						__func__, cmd.cmd);
			}

			if ((rsp.status & 0x7f00))
				rc = rsp.rsp0;
		}
	} else {
		// --- first access so that we can write the rid data
		if ( (status = PC4500_accessrid(ai, rid, CMD_ACCESS)) != 0) {
	                rc = status;
	                goto done;
	        }
		// --- now write the rid data
		if (bap_setup(ai, rid, 0, BAP1) != SUCCESS) {
	                rc = ERROR;
	                goto done;
	        }
		bap_write(ai, pBuf, len, BAP1);
		// ---now commit the rid data
		rc = PC4500_accessrid(ai, rid, 0x100|CMD_ACCESS);
	}
done:
	if (lock)
		up(&ai->sem);
        return rc;
}

/* Allocates a FID to be used for transmitting packets.  We only use
   one for now. */
static u16 transmit_allocate(struct airo_info *ai, int lenPayload, int raw)
{
	unsigned int loop = 3000;
	Cmd cmd;
	Resp rsp;
	u16 txFid;
	__le16 txControl;

	cmd.cmd = CMD_ALLOCATETX;
	cmd.parm0 = lenPayload;
	if (down_interruptible(&ai->sem))
		return ERROR;
	if (issuecommand(ai, &cmd, &rsp) != SUCCESS) {
		txFid = ERROR;
		goto done;
	}
	if ( (rsp.status & 0xFF00) != 0) {
		txFid = ERROR;
		goto done;
	}
	/* wait for the allocate event/indication
	 * It makes me kind of nervous that this can just sit here and spin,
	 * but in practice it only loops like four times. */
	while (((IN4500(ai, EVSTAT) & EV_ALLOC) == 0) && --loop);
	if (!loop) {
		txFid = ERROR;
		goto done;
	}

	// get the allocated fid and acknowledge
	txFid = IN4500(ai, TXALLOCFID);
	OUT4500(ai, EVACK, EV_ALLOC);

	/*  The CARD is pretty cool since it converts the ethernet packet
	 *  into 802.11.  Also note that we don't release the FID since we
	 *  will be using the same one over and over again. */
	/*  We only have to setup the control once since we are not
	 *  releasing the fid. */
	if (raw)
		txControl = cpu_to_le16(TXCTL_TXOK | TXCTL_TXEX | TXCTL_802_11
			| TXCTL_ETHERNET | TXCTL_NORELEASE);
	else
		txControl = cpu_to_le16(TXCTL_TXOK | TXCTL_TXEX | TXCTL_802_3
			| TXCTL_ETHERNET | TXCTL_NORELEASE);
	if (bap_setup(ai, txFid, 0x0008, BAP1) != SUCCESS)
		txFid = ERROR;
	else
		bap_write(ai, &txControl, sizeof(txControl), BAP1);

done:
	up(&ai->sem);

	return txFid;
}

/* In general BAP1 is dedicated to transmiting packets.  However,
   since we need a BAP when accessing RIDs, we also use BAP1 for that.
   Make sure the BAP1 spinlock is held when this is called. */
static int transmit_802_3_packet(struct airo_info *ai, int len, char *pPacket)
{
	__le16 payloadLen;
	Cmd cmd;
	Resp rsp;
	int miclen = 0;
	u16 txFid = len;
	MICBuffer pMic;

	len >>= 16;

	if (len <= ETH_ALEN * 2) {
		airo_print_warn(ai->dev->name, "Short packet %d", len);
		return ERROR;
	}
	len -= ETH_ALEN * 2;

	if (test_bit(FLAG_MIC_CAPABLE, &ai->flags) && ai->micstats.enabled && 
	    (ntohs(((__be16 *)pPacket)[6]) != 0x888E)) {
		if (encapsulate(ai,(etherHead *)pPacket,&pMic,len) != SUCCESS)
			return ERROR;
		miclen = sizeof(pMic);
	}
	// packet is destination[6], source[6], payload[len-12]
	// write the payload length and dst/src/payload
	if (bap_setup(ai, txFid, 0x0036, BAP1) != SUCCESS) return ERROR;
	/* The hardware addresses aren't counted as part of the payload, so
	 * we have to subtract the 12 bytes for the addresses off */
	payloadLen = cpu_to_le16(len + miclen);
	bap_write(ai, &payloadLen, sizeof(payloadLen),BAP1);
	bap_write(ai, (__le16*)pPacket, sizeof(etherHead), BAP1);
	if (miclen)
		bap_write(ai, (__le16*)&pMic, miclen, BAP1);
	bap_write(ai, (__le16*)(pPacket + sizeof(etherHead)), len, BAP1);
	// issue the transmit command
	memset( &cmd, 0, sizeof( cmd ) );
	cmd.cmd = CMD_TRANSMIT;
	cmd.parm0 = txFid;
	if (issuecommand(ai, &cmd, &rsp) != SUCCESS) return ERROR;
	if ( (rsp.status & 0xFF00) != 0) return ERROR;
	return SUCCESS;
}

static int transmit_802_11_packet(struct airo_info *ai, int len, char *pPacket)
{
	__le16 fc, payloadLen;
	Cmd cmd;
	Resp rsp;
	int hdrlen;
	static u8 tail[(30-10) + 2 + 6] = {[30-10] = 6};
	/* padding of header to full size + le16 gaplen (6) + gaplen bytes */
	u16 txFid = len;
	len >>= 16;

	fc = *(__le16*)pPacket;
	hdrlen = header_len(fc);

	if (len < hdrlen) {
		airo_print_warn(ai->dev->name, "Short packet %d", len);
		return ERROR;
	}

	/* packet is 802.11 header +  payload
	 * write the payload length and dst/src/payload */
	if (bap_setup(ai, txFid, 6, BAP1) != SUCCESS) return ERROR;
	/* The 802.11 header aren't counted as part of the payload, so
	 * we have to subtract the header bytes off */
	payloadLen = cpu_to_le16(len-hdrlen);
	bap_write(ai, &payloadLen, sizeof(payloadLen),BAP1);
	if (bap_setup(ai, txFid, 0x0014, BAP1) != SUCCESS) return ERROR;
	bap_write(ai, (__le16 *)pPacket, hdrlen, BAP1);
	bap_write(ai, (__le16 *)(tail + (hdrlen - 10)), 38 - hdrlen, BAP1);

	bap_write(ai, (__le16 *)(pPacket + hdrlen), len - hdrlen, BAP1);
	// issue the transmit command
	memset( &cmd, 0, sizeof( cmd ) );
	cmd.cmd = CMD_TRANSMIT;
	cmd.parm0 = txFid;
	if (issuecommand(ai, &cmd, &rsp) != SUCCESS) return ERROR;
	if ( (rsp.status & 0xFF00) != 0) return ERROR;
	return SUCCESS;
}

/*
 *  This is the proc_fs routines.  It is a bit messier than I would
 *  like!  Feel free to clean it up!
 */

static ssize_t proc_read( struct file *file,
			  char __user *buffer,
			  size_t len,
			  loff_t *offset);

static ssize_t proc_write( struct file *file,
			   const char __user *buffer,
			   size_t len,
			   loff_t *offset );
static int proc_close( struct inode *inode, struct file *file );

static int proc_stats_open( struct inode *inode, struct file *file );
static int proc_statsdelta_open( struct inode *inode, struct file *file );
static int proc_status_open( struct inode *inode, struct file *file );
static int proc_SSID_open( struct inode *inode, struct file *file );
static int proc_APList_open( struct inode *inode, struct file *file );
static int proc_BSSList_open( struct inode *inode, struct file *file );
static int proc_config_open( struct inode *inode, struct file *file );
static int proc_wepkey_open( struct inode *inode, struct file *file );

static const struct file_operations proc_statsdelta_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.open		= proc_statsdelta_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static const struct file_operations proc_stats_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.open		= proc_stats_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static const struct file_operations proc_status_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.open		= proc_status_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static const struct file_operations proc_SSID_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.write		= proc_write,
	.open		= proc_SSID_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static const struct file_operations proc_BSSList_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.write		= proc_write,
	.open		= proc_BSSList_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static const struct file_operations proc_APList_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.write		= proc_write,
	.open		= proc_APList_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static const struct file_operations proc_config_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.write		= proc_write,
	.open		= proc_config_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static const struct file_operations proc_wepkey_ops = {
	.owner		= THIS_MODULE,
	.read		= proc_read,
	.write		= proc_write,
	.open		= proc_wepkey_open,
	.release	= proc_close,
	.llseek		= default_llseek,
};

static struct proc_dir_entry *airo_entry;

struct proc_data {
	int release_buffer;
	int readlen;
	char *rbuffer;
	int writelen;
	int maxwritelen;
	char *wbuffer;
	void (*on_close) (struct inode *, struct file *);
};

static int setup_proc_entry( struct net_device *dev,
			     struct airo_info *apriv ) {
	struct proc_dir_entry *entry;

	/* First setup the device directory */
	strcpy(apriv->proc_name,dev->name);
	apriv->proc_entry = proc_mkdir_mode(apriv->proc_name, airo_perm,
					    airo_entry);
	if (!apriv->proc_entry)
		return -ENOMEM;
	proc_set_user(apriv->proc_entry, proc_kuid, proc_kgid);

	/* Setup the StatsDelta */
	entry = proc_create_data("StatsDelta", 0444 & proc_perm,
				 apriv->proc_entry, &proc_statsdelta_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);

	/* Setup the Stats */
	entry = proc_create_data("Stats", 0444 & proc_perm,
				 apriv->proc_entry, &proc_stats_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);

	/* Setup the Status */
	entry = proc_create_data("Status", 0444 & proc_perm,
				 apriv->proc_entry, &proc_status_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);

	/* Setup the Config */
	entry = proc_create_data("Config", proc_perm,
				 apriv->proc_entry, &proc_config_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);

	/* Setup the SSID */
	entry = proc_create_data("SSID", proc_perm,
				 apriv->proc_entry, &proc_SSID_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);

	/* Setup the APList */
	entry = proc_create_data("APList", proc_perm,
				 apriv->proc_entry, &proc_APList_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);

	/* Setup the BSSList */
	entry = proc_create_data("BSSList", proc_perm,
				 apriv->proc_entry, &proc_BSSList_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);

	/* Setup the WepKey */
	entry = proc_create_data("WepKey", proc_perm,
				 apriv->proc_entry, &proc_wepkey_ops, dev);
	if (!entry)
		goto fail;
	proc_set_user(entry, proc_kuid, proc_kgid);
	return 0;

fail:
	remove_proc_subtree(apriv->proc_name, airo_entry);
	return -ENOMEM;
}

static int takedown_proc_entry( struct net_device *dev,
				struct airo_info *apriv )
{
	remove_proc_subtree(apriv->proc_name, airo_entry);
	return 0;
}

/*
 *  What we want from the proc_fs is to be able to efficiently read
 *  and write the configuration.  To do this, we want to read the
 *  configuration when the file is opened and write it when the file is
 *  closed.  So basically we allocate a read buffer at open and fill it
 *  with data, and allocate a write buffer and read it at close.
 */

/*
 *  The read routine is generic, it relies on the preallocated rbuffer
 *  to supply the data.
 */
static ssize_t proc_read( struct file *file,
			  char __user *buffer,
			  size_t len,
			  loff_t *offset )
{
	struct proc_data *priv = file->private_data;

	if (!priv->rbuffer)
		return -EINVAL;

	return simple_read_from_buffer(buffer, len, offset, priv->rbuffer,
					priv->readlen);
}

/*
 *  The write routine is generic, it fills in a preallocated rbuffer
 *  to supply the data.
 */
static ssize_t proc_write( struct file *file,
			   const char __user *buffer,
			   size_t len,
			   loff_t *offset )
{
	ssize_t ret;
	struct proc_data *priv = file->private_data;

	if (!priv->wbuffer)
		return -EINVAL;

	ret = simple_write_to_buffer(priv->wbuffer, priv->maxwritelen, offset,
					buffer, len);
	if (ret > 0)
		priv->writelen = max_t(int, priv->writelen, *offset);

	return ret;
}

static int proc_status_open(struct inode *inode, struct file *file)
{
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *apriv = dev->ml_priv;
	CapabilityRid cap_rid;
	StatusRid status_rid;
	u16 mode;
	int i;

	if ((file->private_data = kzalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	data = file->private_data;
	if ((data->rbuffer = kmalloc( 2048, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}

	readStatusRid(apriv, &status_rid, 1);
	readCapabilityRid(apriv, &cap_rid, 1);

	mode = le16_to_cpu(status_rid.mode);

        i = sprintf(data->rbuffer, "Status: %s%s%s%s%s%s%s%s%s\n",
                    mode & 1 ? "CFG ": "",
                    mode & 2 ? "ACT ": "",
                    mode & 0x10 ? "SYN ": "",
                    mode & 0x20 ? "LNK ": "",
                    mode & 0x40 ? "LEAP ": "",
                    mode & 0x80 ? "PRIV ": "",
                    mode & 0x100 ? "KEY ": "",
                    mode & 0x200 ? "WEP ": "",
                    mode & 0x8000 ? "ERR ": "");
	sprintf( data->rbuffer+i, "Mode: %x\n"
		 "Signal Strength: %d\n"
		 "Signal Quality: %d\n"
		 "SSID: %-.*s\n"
		 "AP: %-.16s\n"
		 "Freq: %d\n"
		 "BitRate: %dmbs\n"
		 "Driver Version: %s\n"
		 "Device: %s\nManufacturer: %s\nFirmware Version: %s\n"
		 "Radio type: %x\nCountry: %x\nHardware Version: %x\n"
		 "Software Version: %x\nSoftware Subversion: %x\n"
		 "Boot block version: %x\n",
		 le16_to_cpu(status_rid.mode),
		 le16_to_cpu(status_rid.normalizedSignalStrength),
		 le16_to_cpu(status_rid.signalQuality),
		 le16_to_cpu(status_rid.SSIDlen),
		 status_rid.SSID,
		 status_rid.apName,
		 le16_to_cpu(status_rid.channel),
		 le16_to_cpu(status_rid.currentXmitRate) / 2,
		 version,
		 cap_rid.prodName,
		 cap_rid.manName,
		 cap_rid.prodVer,
		 le16_to_cpu(cap_rid.radioType),
		 le16_to_cpu(cap_rid.country),
		 le16_to_cpu(cap_rid.hardVer),
		 le16_to_cpu(cap_rid.softVer),
		 le16_to_cpu(cap_rid.softSubVer),
		 le16_to_cpu(cap_rid.bootBlockVer));
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_stats_rid_open(struct inode*, struct file*, u16);
static int proc_statsdelta_open( struct inode *inode,
				 struct file *file ) {
	if (file->f_mode&FMODE_WRITE) {
		return proc_stats_rid_open(inode, file, RID_STATSDELTACLEAR);
	}
	return proc_stats_rid_open(inode, file, RID_STATSDELTA);
}

static int proc_stats_open( struct inode *inode, struct file *file ) {
	return proc_stats_rid_open(inode, file, RID_STATS);
}

static int proc_stats_rid_open( struct inode *inode,
				struct file *file,
				u16 rid )
{
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *apriv = dev->ml_priv;
	StatsRid stats;
	int i, j;
	__le32 *vals = stats.vals;
	int len;

	if ((file->private_data = kzalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	data = file->private_data;
	if ((data->rbuffer = kmalloc( 4096, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}

	readStatsRid(apriv, &stats, rid, 1);
	len = le16_to_cpu(stats.len);

        j = 0;
	for(i=0; statsLabels[i]!=(char *)-1 && i*4<len; i++) {
		if (!statsLabels[i]) continue;
		if (j+strlen(statsLabels[i])+16>4096) {
			airo_print_warn(apriv->dev->name,
			       "Potentially disastrous buffer overflow averted!");
			break;
		}
		j+=sprintf(data->rbuffer+j, "%s: %u\n", statsLabels[i],
				le32_to_cpu(vals[i]));
	}
	if (i*4 >= len) {
		airo_print_warn(apriv->dev->name, "Got a short rid");
	}
	data->readlen = j;
	return 0;
}

static int get_dec_u16( char *buffer, int *start, int limit ) {
	u16 value;
	int valid = 0;
	for (value = 0; *start < limit && buffer[*start] >= '0' &&
			buffer[*start] <= '9'; (*start)++) {
		valid = 1;
		value *= 10;
		value += buffer[*start] - '0';
	}
	if ( !valid ) return -1;
	return value;
}

static int airo_config_commit(struct net_device *dev,
			      struct iw_request_info *info, void *zwrq,
			      char *extra);

static inline int sniffing_mode(struct airo_info *ai)
{
	return (le16_to_cpu(ai->config.rmode) & le16_to_cpu(RXMODE_MASK)) >=
		le16_to_cpu(RXMODE_RFMON);
}

static void proc_config_on_close(struct inode *inode, struct file *file)
{
	struct proc_data *data = file->private_data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	char *line;

	if ( !data->writelen ) return;

	readConfigRid(ai, 1);
	set_bit (FLAG_COMMIT, &ai->flags);

	line = data->wbuffer;
	while( line[0] ) {
/*** Mode processing */
		if ( !strncmp( line, "Mode: ", 6 ) ) {
			line += 6;
			if (sniffing_mode(ai))
				set_bit (FLAG_RESET, &ai->flags);
			ai->config.rmode &= ~RXMODE_FULL_MASK;
			clear_bit (FLAG_802_11, &ai->flags);
			ai->config.opmode &= ~MODE_CFG_MASK;
			ai->config.scanMode = SCANMODE_ACTIVE;
			if ( line[0] == 'a' ) {
				ai->config.opmode |= MODE_STA_IBSS;
			} else {
				ai->config.opmode |= MODE_STA_ESS;
				if ( line[0] == 'r' ) {
					ai->config.rmode |= RXMODE_RFMON | RXMODE_DISABLE_802_3_HEADER;
					ai->config.scanMode = SCANMODE_PASSIVE;
					set_bit (FLAG_802_11, &ai->flags);
				} else if ( line[0] == 'y' ) {
					ai->config.rmode |= RXMODE_RFMON_ANYBSS | RXMODE_DISABLE_802_3_HEADER;
					ai->config.scanMode = SCANMODE_PASSIVE;
					set_bit (FLAG_802_11, &ai->flags);
				} else if ( line[0] == 'l' )
					ai->config.rmode |= RXMODE_LANMON;
			}
			set_bit (FLAG_COMMIT, &ai->flags);
		}

/*** Radio status */
		else if (!strncmp(line,"Radio: ", 7)) {
			line += 7;
			if (!strncmp(line,"off",3)) {
				set_bit (FLAG_RADIO_OFF, &ai->flags);
			} else {
				clear_bit (FLAG_RADIO_OFF, &ai->flags);
			}
		}
/*** NodeName processing */
		else if ( !strncmp( line, "NodeName: ", 10 ) ) {
			int j;

			line += 10;
			memset( ai->config.nodeName, 0, 16 );
/* Do the name, assume a space between the mode and node name */
			for( j = 0; j < 16 && line[j] != '\n'; j++ ) {
				ai->config.nodeName[j] = line[j];
			}
			set_bit (FLAG_COMMIT, &ai->flags);
		}

/*** PowerMode processing */
		else if ( !strncmp( line, "PowerMode: ", 11 ) ) {
			line += 11;
			if ( !strncmp( line, "PSPCAM", 6 ) ) {
				ai->config.powerSaveMode = POWERSAVE_PSPCAM;
				set_bit (FLAG_COMMIT, &ai->flags);
			} else if ( !strncmp( line, "PSP", 3 ) ) {
				ai->config.powerSaveMode = POWERSAVE_PSP;
				set_bit (FLAG_COMMIT, &ai->flags);
			} else {
				ai->config.powerSaveMode = POWERSAVE_CAM;
				set_bit (FLAG_COMMIT, &ai->flags);
			}
		} else if ( !strncmp( line, "DataRates: ", 11 ) ) {
			int v, i = 0, k = 0; /* i is index into line,
						k is index to rates */

			line += 11;
			while((v = get_dec_u16(line, &i, 3))!=-1) {
				ai->config.rates[k++] = (u8)v;
				line += i + 1;
				i = 0;
			}
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "Channel: ", 9 ) ) {
			int v, i = 0;
			line += 9;
			v = get_dec_u16(line, &i, i+3);
			if ( v != -1 ) {
				ai->config.channelSet = cpu_to_le16(v);
				set_bit (FLAG_COMMIT, &ai->flags);
			}
		} else if ( !strncmp( line, "XmitPower: ", 11 ) ) {
			int v, i = 0;
			line += 11;
			v = get_dec_u16(line, &i, i+3);
			if ( v != -1 ) {
				ai->config.txPower = cpu_to_le16(v);
				set_bit (FLAG_COMMIT, &ai->flags);
			}
		} else if ( !strncmp( line, "WEP: ", 5 ) ) {
			line += 5;
			switch( line[0] ) {
			case 's':
				set_auth_type(ai, AUTH_SHAREDKEY);
				break;
			case 'e':
				set_auth_type(ai, AUTH_ENCRYPT);
				break;
			default:
				set_auth_type(ai, AUTH_OPEN);
				break;
			}
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "LongRetryLimit: ", 16 ) ) {
			int v, i = 0;

			line += 16;
			v = get_dec_u16(line, &i, 3);
			v = (v<0) ? 0 : ((v>255) ? 255 : v);
			ai->config.longRetryLimit = cpu_to_le16(v);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "ShortRetryLimit: ", 17 ) ) {
			int v, i = 0;

			line += 17;
			v = get_dec_u16(line, &i, 3);
			v = (v<0) ? 0 : ((v>255) ? 255 : v);
			ai->config.shortRetryLimit = cpu_to_le16(v);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "RTSThreshold: ", 14 ) ) {
			int v, i = 0;

			line += 14;
			v = get_dec_u16(line, &i, 4);
			v = (v<0) ? 0 : ((v>AIRO_DEF_MTU) ? AIRO_DEF_MTU : v);
			ai->config.rtsThres = cpu_to_le16(v);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "TXMSDULifetime: ", 16 ) ) {
			int v, i = 0;

			line += 16;
			v = get_dec_u16(line, &i, 5);
			v = (v<0) ? 0 : v;
			ai->config.txLifetime = cpu_to_le16(v);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "RXMSDULifetime: ", 16 ) ) {
			int v, i = 0;

			line += 16;
			v = get_dec_u16(line, &i, 5);
			v = (v<0) ? 0 : v;
			ai->config.rxLifetime = cpu_to_le16(v);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "TXDiversity: ", 13 ) ) {
			ai->config.txDiversity =
				(line[13]=='l') ? 1 :
				((line[13]=='r')? 2: 3);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "RXDiversity: ", 13 ) ) {
			ai->config.rxDiversity =
				(line[13]=='l') ? 1 :
				((line[13]=='r')? 2: 3);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if ( !strncmp( line, "FragThreshold: ", 15 ) ) {
			int v, i = 0;

			line += 15;
			v = get_dec_u16(line, &i, 4);
			v = (v<256) ? 256 : ((v>AIRO_DEF_MTU) ? AIRO_DEF_MTU : v);
			v = v & 0xfffe; /* Make sure its even */
			ai->config.fragThresh = cpu_to_le16(v);
			set_bit (FLAG_COMMIT, &ai->flags);
		} else if (!strncmp(line, "Modulation: ", 12)) {
			line += 12;
			switch(*line) {
			case 'd':  ai->config.modulation=MOD_DEFAULT; set_bit(FLAG_COMMIT, &ai->flags); break;
			case 'c':  ai->config.modulation=MOD_CCK; set_bit(FLAG_COMMIT, &ai->flags); break;
			case 'm':  ai->config.modulation=MOD_MOK; set_bit(FLAG_COMMIT, &ai->flags); break;
			default: airo_print_warn(ai->dev->name, "Unknown modulation");
			}
		} else if (!strncmp(line, "Preamble: ", 10)) {
			line += 10;
			switch(*line) {
			case 'a': ai->config.preamble=PREAMBLE_AUTO; set_bit(FLAG_COMMIT, &ai->flags); break;
			case 'l': ai->config.preamble=PREAMBLE_LONG; set_bit(FLAG_COMMIT, &ai->flags); break;
			case 's': ai->config.preamble=PREAMBLE_SHORT; set_bit(FLAG_COMMIT, &ai->flags); break;
			default: airo_print_warn(ai->dev->name, "Unknown preamble");
			}
		} else {
			airo_print_warn(ai->dev->name, "Couldn't figure out %s", line);
		}
		while( line[0] && line[0] != '\n' ) line++;
		if ( line[0] ) line++;
	}
	airo_config_commit(dev, NULL, NULL, NULL);
}

static const char *get_rmode(__le16 mode)
{
        switch(mode & RXMODE_MASK) {
        case RXMODE_RFMON:  return "rfmon";
        case RXMODE_RFMON_ANYBSS:  return "yna (any) bss rfmon";
        case RXMODE_LANMON:  return "lanmon";
        }
        return "ESS";
}

static int proc_config_open(struct inode *inode, struct file *file)
{
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	int i;
	__le16 mode;

	if ((file->private_data = kzalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	data = file->private_data;
	if ((data->rbuffer = kmalloc( 2048, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	if ((data->wbuffer = kzalloc( 2048, GFP_KERNEL )) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->maxwritelen = 2048;
	data->on_close = proc_config_on_close;

	readConfigRid(ai, 1);

	mode = ai->config.opmode & MODE_CFG_MASK;
	i = sprintf( data->rbuffer,
		     "Mode: %s\n"
		     "Radio: %s\n"
		     "NodeName: %-16s\n"
		     "PowerMode: %s\n"
		     "DataRates: %d %d %d %d %d %d %d %d\n"
		     "Channel: %d\n"
		     "XmitPower: %d\n",
		     mode == MODE_STA_IBSS ? "adhoc" :
		     mode == MODE_STA_ESS ? get_rmode(ai->config.rmode):
		     mode == MODE_AP ? "AP" :
		     mode == MODE_AP_RPTR ? "AP RPTR" : "Error",
		     test_bit(FLAG_RADIO_OFF, &ai->flags) ? "off" : "on",
		     ai->config.nodeName,
		     ai->config.powerSaveMode == POWERSAVE_CAM ? "CAM" :
		     ai->config.powerSaveMode == POWERSAVE_PSP ? "PSP" :
		     ai->config.powerSaveMode == POWERSAVE_PSPCAM ? "PSPCAM" :
		     "Error",
		     (int)ai->config.rates[0],
		     (int)ai->config.rates[1],
		     (int)ai->config.rates[2],
		     (int)ai->config.rates[3],
		     (int)ai->config.rates[4],
		     (int)ai->config.rates[5],
		     (int)ai->config.rates[6],
		     (int)ai->config.rates[7],
		     le16_to_cpu(ai->config.channelSet),
		     le16_to_cpu(ai->config.txPower)
		);
	sprintf( data->rbuffer + i,
		 "LongRetryLimit: %d\n"
		 "ShortRetryLimit: %d\n"
		 "RTSThreshold: %d\n"
		 "TXMSDULifetime: %d\n"
		 "RXMSDULifetime: %d\n"
		 "TXDiversity: %s\n"
		 "RXDiversity: %s\n"
		 "FragThreshold: %d\n"
		 "WEP: %s\n"
		 "Modulation: %s\n"
		 "Preamble: %s\n",
		 le16_to_cpu(ai->config.longRetryLimit),
		 le16_to_cpu(ai->config.shortRetryLimit),
		 le16_to_cpu(ai->config.rtsThres),
		 le16_to_cpu(ai->config.txLifetime),
		 le16_to_cpu(ai->config.rxLifetime),
		 ai->config.txDiversity == 1 ? "left" :
		 ai->config.txDiversity == 2 ? "right" : "both",
		 ai->config.rxDiversity == 1 ? "left" :
		 ai->config.rxDiversity == 2 ? "right" : "both",
		 le16_to_cpu(ai->config.fragThresh),
		 ai->config.authType == AUTH_ENCRYPT ? "encrypt" :
		 ai->config.authType == AUTH_SHAREDKEY ? "shared" : "open",
		 ai->config.modulation == MOD_DEFAULT ? "default" :
		 ai->config.modulation == MOD_CCK ? "cck" :
		 ai->config.modulation == MOD_MOK ? "mok" : "error",
		 ai->config.preamble == PREAMBLE_AUTO ? "auto" :
		 ai->config.preamble == PREAMBLE_LONG ? "long" :
		 ai->config.preamble == PREAMBLE_SHORT ? "short" : "error"
		);
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static void proc_SSID_on_close(struct inode *inode, struct file *file)
{
	struct proc_data *data = file->private_data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	SsidRid SSID_rid;
	int i;
	char *p = data->wbuffer;
	char *end = p + data->writelen;

	if (!data->writelen)
		return;

	*end = '\n'; /* sentinel; we have space for it */

	memset(&SSID_rid, 0, sizeof(SSID_rid));

	for (i = 0; i < 3 && p < end; i++) {
		int j = 0;
		/* copy up to 32 characters from this line */
		while (*p != '\n' && j < 32)
			SSID_rid.ssids[i].ssid[j++] = *p++;
		if (j == 0)
			break;
		SSID_rid.ssids[i].len = cpu_to_le16(j);
		/* skip to the beginning of the next line */
		while (*p++ != '\n')
			;
	}
	if (i)
		SSID_rid.len = cpu_to_le16(sizeof(SSID_rid));
	disable_MAC(ai, 1);
	writeSsidRid(ai, &SSID_rid, 1);
	enable_MAC(ai, 1);
}

static void proc_APList_on_close( struct inode *inode, struct file *file ) {
	struct proc_data *data = file->private_data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	APListRid *APList_rid = &ai->APList;
	int i;

	if ( !data->writelen ) return;

	memset(APList_rid, 0, sizeof(*APList_rid));
	APList_rid->len = cpu_to_le16(sizeof(*APList_rid));

	for (i = 0; i < 4 && data->writelen >= (i + 1) * 6 * 3; i++)
		mac_pton(data->wbuffer + i * 6 * 3, APList_rid->ap[i]);

	disable_MAC(ai, 1);
	writeAPListRid(ai, APList_rid, 1);
	enable_MAC(ai, 1);
}

/* This function wraps PC4500_writerid with a MAC disable */
static int do_writerid( struct airo_info *ai, u16 rid, const void *rid_data,
			int len, int dummy ) {
	int rc;

	disable_MAC(ai, 1);
	rc = PC4500_writerid(ai, rid, rid_data, len, 1);
	enable_MAC(ai, 1);
	return rc;
}

/* Returns the WEP key at the specified index, or -1 if that key does
 * not exist.  The buffer is assumed to be at least 16 bytes in length.
 */
static int get_wep_key(struct airo_info *ai, u16 index, char *buf, u16 buflen)
{
	WepKeyRid wkr;
	int rc;
	__le16 lastindex;

	rc = readWepKeyRid(ai, &wkr, 1, 1);
	if (rc != SUCCESS)
		return -1;
	do {
		lastindex = wkr.kindex;
		if (le16_to_cpu(wkr.kindex) == index) {
			int klen = min_t(int, buflen, le16_to_cpu(wkr.klen));
			memcpy(buf, wkr.key, klen);
			return klen;
		}
		rc = readWepKeyRid(ai, &wkr, 0, 1);
		if (rc != SUCCESS)
			return -1;
	} while (lastindex != wkr.kindex);
	return -1;
}

static int get_wep_tx_idx(struct airo_info *ai)
{
	WepKeyRid wkr;
	int rc;
	__le16 lastindex;

	rc = readWepKeyRid(ai, &wkr, 1, 1);
	if (rc != SUCCESS)
		return -1;
	do {
		lastindex = wkr.kindex;
		if (wkr.kindex == cpu_to_le16(0xffff))
			return wkr.mac[0];
		rc = readWepKeyRid(ai, &wkr, 0, 1);
		if (rc != SUCCESS)
			return -1;
	} while (lastindex != wkr.kindex);
	return -1;
}

static int set_wep_key(struct airo_info *ai, u16 index, const char *key,
		       u16 keylen, int perm, int lock)
{
	static const unsigned char macaddr[ETH_ALEN] = { 0x01, 0, 0, 0, 0, 0 };
	WepKeyRid wkr;
	int rc;

	if (WARN_ON(keylen == 0))
		return -1;

	memset(&wkr, 0, sizeof(wkr));
	wkr.len = cpu_to_le16(sizeof(wkr));
	wkr.kindex = cpu_to_le16(index);
	wkr.klen = cpu_to_le16(keylen);
	memcpy(wkr.key, key, keylen);
	memcpy(wkr.mac, macaddr, ETH_ALEN);

	if (perm) disable_MAC(ai, lock);
	rc = writeWepKeyRid(ai, &wkr, perm, lock);
	if (perm) enable_MAC(ai, lock);
	return rc;
}

static int set_wep_tx_idx(struct airo_info *ai, u16 index, int perm, int lock)
{
	WepKeyRid wkr;
	int rc;

	memset(&wkr, 0, sizeof(wkr));
	wkr.len = cpu_to_le16(sizeof(wkr));
	wkr.kindex = cpu_to_le16(0xffff);
	wkr.mac[0] = (char)index;

	if (perm) {
		ai->defindex = (char)index;
		disable_MAC(ai, lock);
	}

	rc = writeWepKeyRid(ai, &wkr, perm, lock);

	if (perm)
		enable_MAC(ai, lock);
	return rc;
}

static void proc_wepkey_on_close( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	int i, rc;
	char key[16];
	u16 index = 0;
	int j = 0;

	memset(key, 0, sizeof(key));

	data = file->private_data;
	if ( !data->writelen ) return;

	if (data->wbuffer[0] >= '0' && data->wbuffer[0] <= '3' &&
	    (data->wbuffer[1] == ' ' || data->wbuffer[1] == '\n')) {
		index = data->wbuffer[0] - '0';
		if (data->wbuffer[1] == '\n') {
			rc = set_wep_tx_idx(ai, index, 1, 1);
			if (rc < 0) {
				airo_print_err(ai->dev->name, "failed to set "
				               "WEP transmit index to %d: %d.",
				               index, rc);
			}
			return;
		}
		j = 2;
	} else {
		airo_print_err(ai->dev->name, "WepKey passed invalid key index");
		return;
	}

	for( i = 0; i < 16*3 && data->wbuffer[i+j]; i++ ) {
		switch(i%3) {
		case 0:
			key[i/3] = hex_to_bin(data->wbuffer[i+j])<<4;
			break;
		case 1:
			key[i/3] |= hex_to_bin(data->wbuffer[i+j]);
			break;
		}
	}

	rc = set_wep_key(ai, index, key, i/3, 1, 1);
	if (rc < 0) {
		airo_print_err(ai->dev->name, "failed to set WEP key at index "
		               "%d: %d.", index, rc);
	}
}

static int proc_wepkey_open( struct inode *inode, struct file *file )
{
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	char *ptr;
	WepKeyRid wkr;
	__le16 lastindex;
	int j=0;
	int rc;

	if ((file->private_data = kzalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(&wkr, 0, sizeof(wkr));
	data = file->private_data;
	if ((data->rbuffer = kzalloc( 180, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->writelen = 0;
	data->maxwritelen = 80;
	if ((data->wbuffer = kzalloc( 80, GFP_KERNEL )) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->on_close = proc_wepkey_on_close;

	ptr = data->rbuffer;
	strcpy(ptr, "No wep keys\n");
	rc = readWepKeyRid(ai, &wkr, 1, 1);
	if (rc == SUCCESS) do {
		lastindex = wkr.kindex;
		if (wkr.kindex == cpu_to_le16(0xffff)) {
			j += sprintf(ptr+j, "Tx key = %d\n",
				     (int)wkr.mac[0]);
		} else {
			j += sprintf(ptr+j, "Key %d set with length = %d\n",
				     le16_to_cpu(wkr.kindex),
				     le16_to_cpu(wkr.klen));
		}
		readWepKeyRid(ai, &wkr, 0, 1);
	} while((lastindex != wkr.kindex) && (j < 180-30));

	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_SSID_open(struct inode *inode, struct file *file)
{
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	int i;
	char *ptr;
	SsidRid SSID_rid;

	if ((file->private_data = kzalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	data = file->private_data;
	if ((data->rbuffer = kmalloc( 104, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->writelen = 0;
	data->maxwritelen = 33*3;
	/* allocate maxwritelen + 1; we'll want a sentinel */
	if ((data->wbuffer = kzalloc(33*3 + 1, GFP_KERNEL)) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->on_close = proc_SSID_on_close;

	readSsidRid(ai, &SSID_rid);
	ptr = data->rbuffer;
	for (i = 0; i < 3; i++) {
		int j;
		size_t len = le16_to_cpu(SSID_rid.ssids[i].len);
		if (!len)
			break;
		if (len > 32)
			len = 32;
		for (j = 0; j < len && SSID_rid.ssids[i].ssid[j]; j++)
			*ptr++ = SSID_rid.ssids[i].ssid[j];
		*ptr++ = '\n';
	}
	*ptr = '\0';
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_APList_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	int i;
	char *ptr;
	APListRid *APList_rid = &ai->APList;

	if ((file->private_data = kzalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	data = file->private_data;
	if ((data->rbuffer = kmalloc( 104, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->writelen = 0;
	data->maxwritelen = 4*6*3;
	if ((data->wbuffer = kzalloc( data->maxwritelen, GFP_KERNEL )) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->on_close = proc_APList_on_close;

	ptr = data->rbuffer;
	for( i = 0; i < 4; i++ ) {
// We end when we find a zero MAC
		if ( !*(int*)APList_rid->ap[i] &&
		     !*(int*)&APList_rid->ap[i][2]) break;
		ptr += sprintf(ptr, "%pM\n", APList_rid->ap[i]);
	}
	if (i==0) ptr += sprintf(ptr, "Not using specific APs\n");

	*ptr = '\0';
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_BSSList_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct net_device *dev = PDE_DATA(inode);
	struct airo_info *ai = dev->ml_priv;
	char *ptr;
	BSSListRid BSSList_rid;
	int rc;
	/* If doLoseSync is not 1, we won't do a Lose Sync */
	int doLoseSync = -1;

	if ((file->private_data = kzalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	data = file->private_data;
	if ((data->rbuffer = kmalloc( 1024, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->writelen = 0;
	data->maxwritelen = 0;
	data->wbuffer = NULL;
	data->on_close = NULL;

	if (file->f_mode & FMODE_WRITE) {
		if (!(file->f_mode & FMODE_READ)) {
			Cmd cmd;
			Resp rsp;

			if (ai->flags & FLAG_RADIO_MASK) return -ENETDOWN;
			memset(&cmd, 0, sizeof(cmd));
			cmd.cmd=CMD_LISTBSS;
			if (down_interruptible(&ai->sem))
				return -ERESTARTSYS;
			issuecommand(ai, &cmd, &rsp);
			up(&ai->sem);
			data->readlen = 0;
			return 0;
		}
		doLoseSync = 1;
	}
	ptr = data->rbuffer;
	/* There is a race condition here if there are concurrent opens.
           Since it is a rare condition, we'll just live with it, otherwise
           we have to add a spin lock... */
	rc = readBSSListRid(ai, doLoseSync, &BSSList_rid);
	while(rc == 0 && BSSList_rid.index != cpu_to_le16(0xffff)) {
		ptr += sprintf(ptr, "%pM %.*s rssi = %d",
			       BSSList_rid.bssid,
				(int)BSSList_rid.ssidLen,
				BSSList_rid.ssid,
				le16_to_cpu(BSSList_rid.dBm));
		ptr += sprintf(ptr, " channel = %d %s %s %s %s\n",
				le16_to_cpu(BSSList_rid.dsChannel),
				BSSList_rid.cap & CAP_ESS ? "ESS" : "",
				BSSList_rid.cap & CAP_IBSS ? "adhoc" : "",
				BSSList_rid.cap & CAP_PRIVACY ? "wep" : "",
				BSSList_rid.cap & CAP_SHORTHDR ? "shorthdr" : "");
		rc = readBSSListRid(ai, 0, &BSSList_rid);
	}
	*ptr = '\0';
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_close( struct inode *inode, struct file *file )
{
	struct proc_data *data = file->private_data;

	if (data->on_close != NULL)
		data->on_close(inode, file);
	kfree(data->rbuffer);
	kfree(data->wbuffer);
	kfree(data);
	return 0;
}

/* Since the card doesn't automatically switch to the right WEP mode,
   we will make it do it.  If the card isn't associated, every secs we
   will switch WEP modes to see if that will help.  If the card is
   associated we will check every minute to see if anything has
   changed. */
static void timer_func( struct net_device *dev ) {
	struct airo_info *apriv = dev->ml_priv;

/* We don't have a link so try changing the authtype */
	readConfigRid(apriv, 0);
	disable_MAC(apriv, 0);
	switch(apriv->config.authType) {
		case AUTH_ENCRYPT:
/* So drop to OPEN */
			apriv->config.authType = AUTH_OPEN;
			break;
		case AUTH_SHAREDKEY:
			if (apriv->keyindex < auto_wep) {
				set_wep_tx_idx(apriv, apriv->keyindex, 0, 0);
				apriv->config.authType = AUTH_SHAREDKEY;
				apriv->keyindex++;
			} else {
			        /* Drop to ENCRYPT */
				apriv->keyindex = 0;
				set_wep_tx_idx(apriv, apriv->defindex, 0, 0);
				apriv->config.authType = AUTH_ENCRYPT;
			}
			break;
		default:  /* We'll escalate to SHAREDKEY */
			apriv->config.authType = AUTH_SHAREDKEY;
	}
	set_bit (FLAG_COMMIT, &apriv->flags);
	writeConfigRid(apriv, 0);
	enable_MAC(apriv, 0);
	up(&apriv->sem);

/* Schedule check to see if the change worked */
	clear_bit(JOB_AUTOWEP, &apriv->jobs);
	apriv->expires = RUN_AT(HZ*3);
}

#ifdef CONFIG_PCI
static int airo_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *pent)
{
	struct net_device *dev;

	if (pci_enable_device(pdev))
		return -ENODEV;
	pci_set_master(pdev);

	if (pdev->device == 0x5000 || pdev->device == 0xa504)
			dev = _init_airo_card(pdev->irq, pdev->resource[0].start, 0, pdev, &pdev->dev);
	else
			dev = _init_airo_card(pdev->irq, pdev->resource[2].start, 0, pdev, &pdev->dev);
	if (!dev) {
		pci_disable_device(pdev);
		return -ENODEV;
	}

	pci_set_drvdata(pdev, dev);
	return 0;
}

static void airo_pci_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	airo_print_info(dev->name, "Unregistering...");
	stop_airo_card(dev, 1);
	pci_disable_device(pdev);
}

static int airo_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct airo_info *ai = dev->ml_priv;
	Cmd cmd;
	Resp rsp;

	if (!ai->SSID)
		ai->SSID = kmalloc(sizeof(SsidRid), GFP_KERNEL);
	if (!ai->SSID)
		return -ENOMEM;
	readSsidRid(ai, ai->SSID);
	memset(&cmd, 0, sizeof(cmd));
	/* the lock will be released at the end of the resume callback */
	if (down_interruptible(&ai->sem))
		return -EAGAIN;
	disable_MAC(ai, 0);
	netif_device_detach(dev);
	ai->power = state;
	cmd.cmd = HOSTSLEEP;
	issuecommand(ai, &cmd, &rsp);

	pci_enable_wake(pdev, pci_choose_state(pdev, state), 1);
	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int airo_pci_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct airo_info *ai = dev->ml_priv;
	pci_power_t prev_state = pdev->current_state;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);

	if (prev_state != PCI_D1) {
		reset_card(dev, 0);
		mpi_init_descriptors(ai);
		setup_card(ai, dev->dev_addr, 0);
		clear_bit(FLAG_RADIO_OFF, &ai->flags);
		clear_bit(FLAG_PENDING_XMIT, &ai->flags);
	} else {
		OUT4500(ai, EVACK, EV_AWAKEN);
		OUT4500(ai, EVACK, EV_AWAKEN);
		msleep(100);
	}

	set_bit(FLAG_COMMIT, &ai->flags);
	disable_MAC(ai, 0);
        msleep(200);
	if (ai->SSID) {
		writeSsidRid(ai, ai->SSID, 0);
		kfree(ai->SSID);
		ai->SSID = NULL;
	}
	writeAPListRid(ai, &ai->APList, 0);
	writeConfigRid(ai, 0);
	enable_MAC(ai, 0);
	ai->power = PMSG_ON;
	netif_device_attach(dev);
	netif_wake_queue(dev);
	enable_interrupts(ai);
	up(&ai->sem);
	return 0;
}
#endif

static int __init airo_init_module( void )
{
	int i;

	proc_kuid = make_kuid(&init_user_ns, proc_uid);
	proc_kgid = make_kgid(&init_user_ns, proc_gid);
	if (!uid_valid(proc_kuid) || !gid_valid(proc_kgid))
		return -EINVAL;

	airo_entry = proc_mkdir_mode("driver/aironet", airo_perm, NULL);

	if (airo_entry)
		proc_set_user(airo_entry, proc_kuid, proc_kgid);

	for (i = 0; i < 4 && io[i] && irq[i]; i++) {
		airo_print_info("", "Trying to configure ISA adapter at irq=%d "
			"io=0x%x", irq[i], io[i] );
		if (init_airo_card( irq[i], io[i], 0, NULL ))
			/* do nothing */ ;
	}

#ifdef CONFIG_PCI
	airo_print_info("", "Probing for PCI adapters");
	i = pci_register_driver(&airo_driver);
	airo_print_info("", "Finished probing for PCI adapters");

	if (i) {
		remove_proc_entry("driver/aironet", NULL);
		return i;
	}
#endif

	/* Always exit with success, as we are a library module
	 * as well as a driver module
	 */
	return 0;
}

static void __exit airo_cleanup_module( void )
{
	struct airo_info *ai;
	while(!list_empty(&airo_devices)) {
		ai = list_entry(airo_devices.next, struct airo_info, dev_list);
		airo_print_info(ai->dev->name, "Unregistering...");
		stop_airo_card(ai->dev, 1);
	}
#ifdef CONFIG_PCI
	pci_unregister_driver(&airo_driver);
#endif
	remove_proc_entry("driver/aironet", NULL);
}

/*
 * Initial Wireless Extension code for Aironet driver by :
 *	Jean Tourrilhes <jt@hpl.hp.com> - HPL - 17 November 00
 * Conversion to new driver API by :
 *	Jean Tourrilhes <jt@hpl.hp.com> - HPL - 26 March 02
 * Javier also did a good amount of work here, adding some new extensions
 * and fixing my code. Let's just say that without him this code just
 * would not work at all... - Jean II
 */

static u8 airo_rssi_to_dbm (tdsRssiEntry *rssi_rid, u8 rssi)
{
	if (!rssi_rid)
		return 0;

	return (0x100 - rssi_rid[rssi].rssidBm);
}

static u8 airo_dbm_to_pct (tdsRssiEntry *rssi_rid, u8 dbm)
{
	int i;

	if (!rssi_rid)
		return 0;

	for (i = 0; i < 256; i++)
		if (rssi_rid[i].rssidBm == dbm)
			return rssi_rid[i].rssipct;

	return 0;
}


static int airo_get_quality (StatusRid *status_rid, CapabilityRid *cap_rid)
{
	int quality = 0;
	u16 sq;

	if ((status_rid->mode & cpu_to_le16(0x3f)) != cpu_to_le16(0x3f))
		return 0;

	if (!(cap_rid->hardCap & cpu_to_le16(8)))
		return 0;

	sq = le16_to_cpu(status_rid->signalQuality);
	if (memcmp(cap_rid->prodName, "350", 3))
		if (sq > 0x20)
			quality = 0;
		else
			quality = 0x20 - sq;
	else
		if (sq > 0xb0)
			quality = 0;
		else if (sq < 0x10)
			quality = 0xa0;
		else
			quality = 0xb0 - sq;
	return quality;
}

#define airo_get_max_quality(cap_rid) (memcmp((cap_rid)->prodName, "350", 3) ? 0x20 : 0xa0)
#define airo_get_avg_quality(cap_rid) (memcmp((cap_rid)->prodName, "350", 3) ? 0x10 : 0x50);

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get protocol name
 */
static int airo_get_name(struct net_device *dev,
			 struct iw_request_info *info,
			 char *cwrq,
			 char *extra)
{
	strcpy(cwrq, "IEEE 802.11-DS");
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set frequency
 */
static int airo_set_freq(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_freq *fwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;
	int rc = -EINPROGRESS;		/* Call commit handler */

	/* If setting by frequency, convert to a channel */
	if(fwrq->e == 1) {
		int f = fwrq->m / 100000;

		/* Hack to fall through... */
		fwrq->e = 0;
		fwrq->m = ieee80211_frequency_to_channel(f);
	}
	/* Setting by channel number */
	if (fwrq->m < 0 || fwrq->m > 1000 || fwrq->e > 0)
		rc = -EOPNOTSUPP;
	else {
		int channel = fwrq->m;
		/* We should do a better check than that,
		 * based on the card capability !!! */
		if((channel < 1) || (channel > 14)) {
			airo_print_dbg(dev->name, "New channel value of %d is invalid!",
				fwrq->m);
			rc = -EINVAL;
		} else {
			readConfigRid(local, 1);
			/* Yes ! We can set it !!! */
			local->config.channelSet = cpu_to_le16(channel);
			set_bit (FLAG_COMMIT, &local->flags);
		}
	}
	return rc;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get frequency
 */
static int airo_get_freq(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_freq *fwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;
	StatusRid status_rid;		/* Card status info */
	int ch;

	readConfigRid(local, 1);
	if ((local->config.opmode & MODE_CFG_MASK) == MODE_STA_ESS)
		status_rid.channel = local->config.channelSet;
	else
		readStatusRid(local, &status_rid, 1);

	ch = le16_to_cpu(status_rid.channel);
	if((ch > 0) && (ch < 15)) {
		fwrq->m = 100000 *
			ieee80211_channel_to_frequency(ch, NL80211_BAND_2GHZ);
		fwrq->e = 1;
	} else {
		fwrq->m = ch;
		fwrq->e = 0;
	}

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set ESSID
 */
static int airo_set_essid(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;
	SsidRid SSID_rid;		/* SSIDs */

	/* Reload the list of current SSID */
	readSsidRid(local, &SSID_rid);

	/* Check if we asked for `any' */
	if (dwrq->flags == 0) {
		/* Just send an empty SSID list */
		memset(&SSID_rid, 0, sizeof(SSID_rid));
	} else {
		unsigned index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

		/* Check the size of the string */
		if (dwrq->length > IW_ESSID_MAX_SIZE)
			return -E2BIG ;

		/* Check if index is valid */
		if (index >= ARRAY_SIZE(SSID_rid.ssids))
			return -EINVAL;

		/* Set the SSID */
		memset(SSID_rid.ssids[index].ssid, 0,
		       sizeof(SSID_rid.ssids[index].ssid));
		memcpy(SSID_rid.ssids[index].ssid, extra, dwrq->length);
		SSID_rid.ssids[index].len = cpu_to_le16(dwrq->length);
	}
	SSID_rid.len = cpu_to_le16(sizeof(SSID_rid));
	/* Write it to the card */
	disable_MAC(local, 1);
	writeSsidRid(local, &SSID_rid, 1);
	enable_MAC(local, 1);

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get ESSID
 */
static int airo_get_essid(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;
	StatusRid status_rid;		/* Card status info */

	readStatusRid(local, &status_rid, 1);

	/* Note : if dwrq->flags != 0, we should
	 * get the relevant SSID from the SSID list... */

	/* Get the current SSID */
	memcpy(extra, status_rid.SSID, le16_to_cpu(status_rid.SSIDlen));
	/* If none, we may want to get the one that was set */

	/* Push it out ! */
	dwrq->length = le16_to_cpu(status_rid.SSIDlen);
	dwrq->flags = 1; /* active */

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set AP address
 */
static int airo_set_wap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *awrq,
			char *extra)
{
	struct airo_info *local = dev->ml_priv;
	Cmd cmd;
	Resp rsp;
	APListRid *APList_rid = &local->APList;

	if (awrq->sa_family != ARPHRD_ETHER)
		return -EINVAL;
	else if (is_broadcast_ether_addr(awrq->sa_data) ||
		 is_zero_ether_addr(awrq->sa_data)) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd=CMD_LOSE_SYNC;
		if (down_interruptible(&local->sem))
			return -ERESTARTSYS;
		issuecommand(local, &cmd, &rsp);
		up(&local->sem);
	} else {
		memset(APList_rid, 0, sizeof(*APList_rid));
		APList_rid->len = cpu_to_le16(sizeof(*APList_rid));
		memcpy(APList_rid->ap[0], awrq->sa_data, ETH_ALEN);
		disable_MAC(local, 1);
		writeAPListRid(local, APList_rid, 1);
		enable_MAC(local, 1);
	}
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get AP address
 */
static int airo_get_wap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *awrq,
			char *extra)
{
	struct airo_info *local = dev->ml_priv;
	StatusRid status_rid;		/* Card status info */

	readStatusRid(local, &status_rid, 1);

	/* Tentative. This seems to work, wow, I'm lucky !!! */
	memcpy(awrq->sa_data, status_rid.bssid[0], ETH_ALEN);
	awrq->sa_family = ARPHRD_ETHER;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Nickname
 */
static int airo_set_nick(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *dwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;

	/* Check the size of the string */
	if(dwrq->length > 16) {
		return -E2BIG;
	}
	readConfigRid(local, 1);
	memset(local->config.nodeName, 0, sizeof(local->config.nodeName));
	memcpy(local->config.nodeName, extra, dwrq->length);
	set_bit (FLAG_COMMIT, &local->flags);

	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Nickname
 */
static int airo_get_nick(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *dwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	strncpy(extra, local->config.nodeName, 16);
	extra[16] = '\0';
	dwrq->length = strlen(extra);

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Bit-Rate
 */
static int airo_set_rate(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *vwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;
	CapabilityRid cap_rid;		/* Card capability info */
	u8	brate = 0;
	int	i;

	/* First : get a valid bit rate value */
	readCapabilityRid(local, &cap_rid, 1);

	/* Which type of value ? */
	if((vwrq->value < 8) && (vwrq->value >= 0)) {
		/* Setting by rate index */
		/* Find value in the magic rate table */
		brate = cap_rid.supportedRates[vwrq->value];
	} else {
		/* Setting by frequency value */
		u8	normvalue = (u8) (vwrq->value/500000);

		/* Check if rate is valid */
		for(i = 0 ; i < 8 ; i++) {
			if(normvalue == cap_rid.supportedRates[i]) {
				brate = normvalue;
				break;
			}
		}
	}
	/* -1 designed the max rate (mostly auto mode) */
	if(vwrq->value == -1) {
		/* Get the highest available rate */
		for(i = 0 ; i < 8 ; i++) {
			if(cap_rid.supportedRates[i] == 0)
				break;
		}
		if(i != 0)
			brate = cap_rid.supportedRates[i - 1];
	}
	/* Check that it is valid */
	if(brate == 0) {
		return -EINVAL;
	}

	readConfigRid(local, 1);
	/* Now, check if we want a fixed or auto value */
	if(vwrq->fixed == 0) {
		/* Fill all the rates up to this max rate */
		memset(local->config.rates, 0, 8);
		for(i = 0 ; i < 8 ; i++) {
			local->config.rates[i] = cap_rid.supportedRates[i];
			if(local->config.rates[i] == brate)
				break;
		}
	} else {
		/* Fixed mode */
		/* One rate, fixed */
		memset(local->config.rates, 0, 8);
		local->config.rates[0] = brate;
	}
	set_bit (FLAG_COMMIT, &local->flags);

	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Bit-Rate
 */
static int airo_get_rate(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *vwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;
	StatusRid status_rid;		/* Card status info */

	readStatusRid(local, &status_rid, 1);

	vwrq->value = le16_to_cpu(status_rid.currentXmitRate) * 500000;
	/* If more than one rate, set auto */
	readConfigRid(local, 1);
	vwrq->fixed = (local->config.rates[1] == 0);

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set RTS threshold
 */
static int airo_set_rts(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *vwrq,
			char *extra)
{
	struct airo_info *local = dev->ml_priv;
	int rthr = vwrq->value;

	if(vwrq->disabled)
		rthr = AIRO_DEF_MTU;
	if((rthr < 0) || (rthr > AIRO_DEF_MTU)) {
		return -EINVAL;
	}
	readConfigRid(local, 1);
	local->config.rtsThres = cpu_to_le16(rthr);
	set_bit (FLAG_COMMIT, &local->flags);

	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get RTS threshold
 */
static int airo_get_rts(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *vwrq,
			char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	vwrq->value = le16_to_cpu(local->config.rtsThres);
	vwrq->disabled = (vwrq->value >= AIRO_DEF_MTU);
	vwrq->fixed = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Fragmentation threshold
 */
static int airo_set_frag(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *vwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;
	int fthr = vwrq->value;

	if(vwrq->disabled)
		fthr = AIRO_DEF_MTU;
	if((fthr < 256) || (fthr > AIRO_DEF_MTU)) {
		return -EINVAL;
	}
	fthr &= ~0x1;	/* Get an even value - is it really needed ??? */
	readConfigRid(local, 1);
	local->config.fragThresh = cpu_to_le16(fthr);
	set_bit (FLAG_COMMIT, &local->flags);

	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Fragmentation threshold
 */
static int airo_get_frag(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *vwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	vwrq->value = le16_to_cpu(local->config.fragThresh);
	vwrq->disabled = (vwrq->value >= AIRO_DEF_MTU);
	vwrq->fixed = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Mode of Operation
 */
static int airo_set_mode(struct net_device *dev,
			 struct iw_request_info *info,
			 __u32 *uwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;
	int reset = 0;

	readConfigRid(local, 1);
	if (sniffing_mode(local))
		reset = 1;

	switch(*uwrq) {
		case IW_MODE_ADHOC:
			local->config.opmode &= ~MODE_CFG_MASK;
			local->config.opmode |= MODE_STA_IBSS;
			local->config.rmode &= ~RXMODE_FULL_MASK;
			local->config.scanMode = SCANMODE_ACTIVE;
			clear_bit (FLAG_802_11, &local->flags);
			break;
		case IW_MODE_INFRA:
			local->config.opmode &= ~MODE_CFG_MASK;
			local->config.opmode |= MODE_STA_ESS;
			local->config.rmode &= ~RXMODE_FULL_MASK;
			local->config.scanMode = SCANMODE_ACTIVE;
			clear_bit (FLAG_802_11, &local->flags);
			break;
		case IW_MODE_MASTER:
			local->config.opmode &= ~MODE_CFG_MASK;
			local->config.opmode |= MODE_AP;
			local->config.rmode &= ~RXMODE_FULL_MASK;
			local->config.scanMode = SCANMODE_ACTIVE;
			clear_bit (FLAG_802_11, &local->flags);
			break;
		case IW_MODE_REPEAT:
			local->config.opmode &= ~MODE_CFG_MASK;
			local->config.opmode |= MODE_AP_RPTR;
			local->config.rmode &= ~RXMODE_FULL_MASK;
			local->config.scanMode = SCANMODE_ACTIVE;
			clear_bit (FLAG_802_11, &local->flags);
			break;
		case IW_MODE_MONITOR:
			local->config.opmode &= ~MODE_CFG_MASK;
			local->config.opmode |= MODE_STA_ESS;
			local->config.rmode &= ~RXMODE_FULL_MASK;
			local->config.rmode |= RXMODE_RFMON | RXMODE_DISABLE_802_3_HEADER;
			local->config.scanMode = SCANMODE_PASSIVE;
			set_bit (FLAG_802_11, &local->flags);
			break;
		default:
			return -EINVAL;
	}
	if (reset)
		set_bit (FLAG_RESET, &local->flags);
	set_bit (FLAG_COMMIT, &local->flags);

	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Mode of Operation
 */
static int airo_get_mode(struct net_device *dev,
			 struct iw_request_info *info,
			 __u32 *uwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	/* If not managed, assume it's ad-hoc */
	switch (local->config.opmode & MODE_CFG_MASK) {
		case MODE_STA_ESS:
			*uwrq = IW_MODE_INFRA;
			break;
		case MODE_AP:
			*uwrq = IW_MODE_MASTER;
			break;
		case MODE_AP_RPTR:
			*uwrq = IW_MODE_REPEAT;
			break;
		default:
			*uwrq = IW_MODE_ADHOC;
	}

	return 0;
}

static inline int valid_index(struct airo_info *ai, int index)
{
	return (index >= 0) && (index <= ai->max_wep_idx);
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Encryption Key
 */
static int airo_set_encode(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *dwrq,
			   char *extra)
{
	struct airo_info *local = dev->ml_priv;
	int perm = (dwrq->flags & IW_ENCODE_TEMP ? 0 : 1);
	__le16 currentAuthType = local->config.authType;
	int rc = 0;

	if (!local->wep_capable)
		return -EOPNOTSUPP;

	readConfigRid(local, 1);

	/* Basic checking: do we have a key to set ?
	 * Note : with the new API, it's impossible to get a NULL pointer.
	 * Therefore, we need to check a key size == 0 instead.
	 * New version of iwconfig properly set the IW_ENCODE_NOKEY flag
	 * when no key is present (only change flags), but older versions
	 * don't do it. - Jean II */
	if (dwrq->length > 0) {
		wep_key_t key;
		int index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
		int current_index;

		/* Check the size of the key */
		if (dwrq->length > MAX_KEY_SIZE) {
			return -EINVAL;
		}

		current_index = get_wep_tx_idx(local);
		if (current_index < 0)
			current_index = 0;

		/* Check the index (none -> use current) */
		if (!valid_index(local, index))
			index = current_index;

		/* Set the length */
		if (dwrq->length > MIN_KEY_SIZE)
			key.len = MAX_KEY_SIZE;
		else
			key.len = MIN_KEY_SIZE;
		/* Check if the key is not marked as invalid */
		if(!(dwrq->flags & IW_ENCODE_NOKEY)) {
			/* Cleanup */
			memset(key.key, 0, MAX_KEY_SIZE);
			/* Copy the key in the driver */
			memcpy(key.key, extra, dwrq->length);
			/* Send the key to the card */
			rc = set_wep_key(local, index, key.key, key.len, perm, 1);
			if (rc < 0) {
				airo_print_err(local->dev->name, "failed to set"
				               " WEP key at index %d: %d.",
				               index, rc);
				return rc;
			}
		}
		/* WE specify that if a valid key is set, encryption
		 * should be enabled (user may turn it off later)
		 * This is also how "iwconfig ethX key on" works */
		if((index == current_index) && (key.len > 0) &&
		   (local->config.authType == AUTH_OPEN))
			set_auth_type(local, AUTH_ENCRYPT);
	} else {
		/* Do we want to just set the transmit key index ? */
		int index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
		if (valid_index(local, index)) {
			rc = set_wep_tx_idx(local, index, perm, 1);
			if (rc < 0) {
				airo_print_err(local->dev->name, "failed to set"
				               " WEP transmit index to %d: %d.",
				               index, rc);
				return rc;
			}
		} else {
			/* Don't complain if only change the mode */
			if (!(dwrq->flags & IW_ENCODE_MODE))
				return -EINVAL;
		}
	}
	/* Read the flags */
	if (dwrq->flags & IW_ENCODE_DISABLED)
		set_auth_type(local, AUTH_OPEN);	/* disable encryption */
	if(dwrq->flags & IW_ENCODE_RESTRICTED)
		set_auth_type(local, AUTH_SHAREDKEY);	/* Only Both */
	if (dwrq->flags & IW_ENCODE_OPEN)
		set_auth_type(local, AUTH_ENCRYPT);	/* Only Wep */
	/* Commit the changes to flags if needed */
	if (local->config.authType != currentAuthType)
		set_bit (FLAG_COMMIT, &local->flags);
	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Encryption Key
 */
static int airo_get_encode(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *dwrq,
			   char *extra)
{
	struct airo_info *local = dev->ml_priv;
	int index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
	int wep_key_len;
	u8 buf[16];

	if (!local->wep_capable)
		return -EOPNOTSUPP;

	readConfigRid(local, 1);

	/* Check encryption mode */
	switch(local->config.authType)	{
		case AUTH_ENCRYPT:
			dwrq->flags = IW_ENCODE_OPEN;
			break;
		case AUTH_SHAREDKEY:
			dwrq->flags = IW_ENCODE_RESTRICTED;
			break;
		default:
		case AUTH_OPEN:
			dwrq->flags = IW_ENCODE_DISABLED;
			break;
	}
	/* We can't return the key, so set the proper flag and return zero */
	dwrq->flags |= IW_ENCODE_NOKEY;
	memset(extra, 0, 16);

	/* Which key do we want ? -1 -> tx index */
	if (!valid_index(local, index)) {
		index = get_wep_tx_idx(local);
		if (index < 0)
			index = 0;
	}
	dwrq->flags |= index + 1;

	/* Copy the key to the user buffer */
	wep_key_len = get_wep_key(local, index, &buf[0], sizeof(buf));
	if (wep_key_len < 0) {
		dwrq->length = 0;
	} else {
		dwrq->length = wep_key_len;
		memcpy(extra, buf, dwrq->length);
	}

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set extended Encryption parameters
 */
static int airo_set_encodeext(struct net_device *dev,
			   struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	struct airo_info *local = dev->ml_priv;
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int perm = ( encoding->flags & IW_ENCODE_TEMP ? 0 : 1 );
	__le16 currentAuthType = local->config.authType;
	int idx, key_len, alg = ext->alg, set_key = 1, rc;
	wep_key_t key;

	if (!local->wep_capable)
		return -EOPNOTSUPP;

	readConfigRid(local, 1);

	/* Determine and validate the key index */
	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if (!valid_index(local, idx - 1))
			return -EINVAL;
		idx--;
	} else {
		idx = get_wep_tx_idx(local);
		if (idx < 0)
			idx = 0;
	}

	if (encoding->flags & IW_ENCODE_DISABLED)
		alg = IW_ENCODE_ALG_NONE;

	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
		/* Only set transmit key index here, actual
		 * key is set below if needed.
		 */
		rc = set_wep_tx_idx(local, idx, perm, 1);
		if (rc < 0) {
			airo_print_err(local->dev->name, "failed to set "
			               "WEP transmit index to %d: %d.",
			               idx, rc);
			return rc;
		}
		set_key = ext->key_len > 0 ? 1 : 0;
	}

	if (set_key) {
		/* Set the requested key first */
		memset(key.key, 0, MAX_KEY_SIZE);
		switch (alg) {
		case IW_ENCODE_ALG_NONE:
			key.len = 0;
			break;
		case IW_ENCODE_ALG_WEP:
			if (ext->key_len > MIN_KEY_SIZE) {
				key.len = MAX_KEY_SIZE;
			} else if (ext->key_len > 0) {
				key.len = MIN_KEY_SIZE;
			} else {
				return -EINVAL;
			}
			key_len = min (ext->key_len, key.len);
			memcpy(key.key, ext->key, key_len);
			break;
		default:
			return -EINVAL;
		}
		if (key.len == 0) {
			rc = set_wep_tx_idx(local, idx, perm, 1);
			if (rc < 0) {
				airo_print_err(local->dev->name,
					       "failed to set WEP transmit index to %d: %d.",
					       idx, rc);
				return rc;
			}
		} else {
			rc = set_wep_key(local, idx, key.key, key.len, perm, 1);
			if (rc < 0) {
				airo_print_err(local->dev->name,
					       "failed to set WEP key at index %d: %d.",
					       idx, rc);
				return rc;
			}
		}
	}

	/* Read the flags */
	if (encoding->flags & IW_ENCODE_DISABLED)
		set_auth_type(local, AUTH_OPEN);	/* disable encryption */
	if(encoding->flags & IW_ENCODE_RESTRICTED)
		set_auth_type(local, AUTH_SHAREDKEY);	/* Only Both */
	if (encoding->flags & IW_ENCODE_OPEN)
		set_auth_type(local, AUTH_ENCRYPT);
	/* Commit the changes to flags if needed */
	if (local->config.authType != currentAuthType)
		set_bit (FLAG_COMMIT, &local->flags);

	return -EINPROGRESS;
}


/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get extended Encryption parameters
 */
static int airo_get_encodeext(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu,
			    char *extra)
{
	struct airo_info *local = dev->ml_priv;
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int idx, max_key_len, wep_key_len;
	u8 buf[16];

	if (!local->wep_capable)
		return -EOPNOTSUPP;

	readConfigRid(local, 1);

	max_key_len = encoding->length - sizeof(*ext);
	if (max_key_len < 0)
		return -EINVAL;

	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if (!valid_index(local, idx - 1))
			return -EINVAL;
		idx--;
	} else {
		idx = get_wep_tx_idx(local);
		if (idx < 0)
			idx = 0;
	}

	encoding->flags = idx + 1;
	memset(ext, 0, sizeof(*ext));

	/* Check encryption mode */
	switch(local->config.authType) {
		case AUTH_ENCRYPT:
			encoding->flags = IW_ENCODE_ALG_WEP | IW_ENCODE_ENABLED;
			break;
		case AUTH_SHAREDKEY:
			encoding->flags = IW_ENCODE_ALG_WEP | IW_ENCODE_ENABLED;
			break;
		default:
		case AUTH_OPEN:
			encoding->flags = IW_ENCODE_ALG_NONE | IW_ENCODE_DISABLED;
			break;
	}
	/* We can't return the key, so set the proper flag and return zero */
	encoding->flags |= IW_ENCODE_NOKEY;
	memset(extra, 0, 16);
	
	/* Copy the key to the user buffer */
	wep_key_len = get_wep_key(local, idx, &buf[0], sizeof(buf));
	if (wep_key_len < 0) {
		ext->key_len = 0;
	} else {
		ext->key_len = wep_key_len;
		memcpy(extra, buf, ext->key_len);
	}

	return 0;
}


/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set extended authentication parameters
 */
static int airo_set_auth(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct airo_info *local = dev->ml_priv;
	struct iw_param *param = &wrqu->param;
	__le16 currentAuthType = local->config.authType;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_PRIVACY_INVOKED:
		/*
		 * airo does not use these parameters
		 */
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		if (param->value) {
			/* Only change auth type if unencrypted */
			if (currentAuthType == AUTH_OPEN)
				set_auth_type(local, AUTH_ENCRYPT);
		} else {
			set_auth_type(local, AUTH_OPEN);
		}

		/* Commit the changes to flags if needed */
		if (local->config.authType != currentAuthType)
			set_bit (FLAG_COMMIT, &local->flags);
		break;

	case IW_AUTH_80211_AUTH_ALG: {
			if (param->value & IW_AUTH_ALG_SHARED_KEY) {
				set_auth_type(local, AUTH_SHAREDKEY);
			} else if (param->value & IW_AUTH_ALG_OPEN_SYSTEM) {
				/* We don't know here if WEP open system or
				 * unencrypted mode was requested - so use the
				 * last mode (of these two) used last time
				 */
				set_auth_type(local, local->last_auth);
			} else
				return -EINVAL;

			/* Commit the changes to flags if needed */
			if (local->config.authType != currentAuthType)
				set_bit (FLAG_COMMIT, &local->flags);
			break;
		}

	case IW_AUTH_WPA_ENABLED:
		/* Silently accept disable of WPA */
		if (param->value > 0)
			return -EOPNOTSUPP;
		break;

	default:
		return -EOPNOTSUPP;
	}
	return -EINPROGRESS;
}


/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get extended authentication parameters
 */
static int airo_get_auth(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct airo_info *local = dev->ml_priv;
	struct iw_param *param = &wrqu->param;
	__le16 currentAuthType = local->config.authType;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_DROP_UNENCRYPTED:
		switch (currentAuthType) {
		case AUTH_SHAREDKEY:
		case AUTH_ENCRYPT:
			param->value = 1;
			break;
		default:
			param->value = 0;
			break;
		}
		break;

	case IW_AUTH_80211_AUTH_ALG:
		switch (currentAuthType) {
		case AUTH_SHAREDKEY:
			param->value = IW_AUTH_ALG_SHARED_KEY;
			break;
		case AUTH_ENCRYPT:
		default:
			param->value = IW_AUTH_ALG_OPEN_SYSTEM;
			break;
		}
		break;

	case IW_AUTH_WPA_ENABLED:
		param->value = 0;
		break;

	default:
		return -EOPNOTSUPP;
	}
	return 0;
}


/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Tx-Power
 */
static int airo_set_txpow(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;
	CapabilityRid cap_rid;		/* Card capability info */
	int i;
	int rc = -EINVAL;
	__le16 v = cpu_to_le16(vwrq->value);

	readCapabilityRid(local, &cap_rid, 1);

	if (vwrq->disabled) {
		set_bit (FLAG_RADIO_OFF, &local->flags);
		set_bit (FLAG_COMMIT, &local->flags);
		return -EINPROGRESS;		/* Call commit handler */
	}
	if (vwrq->flags != IW_TXPOW_MWATT) {
		return -EINVAL;
	}
	clear_bit (FLAG_RADIO_OFF, &local->flags);
	for (i = 0; i < 8 && cap_rid.txPowerLevels[i]; i++)
		if (v == cap_rid.txPowerLevels[i]) {
			readConfigRid(local, 1);
			local->config.txPower = v;
			set_bit (FLAG_COMMIT, &local->flags);
			rc = -EINPROGRESS;	/* Call commit handler */
			break;
		}
	return rc;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Tx-Power
 */
static int airo_get_txpow(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	vwrq->value = le16_to_cpu(local->config.txPower);
	vwrq->fixed = 1;	/* No power control */
	vwrq->disabled = test_bit(FLAG_RADIO_OFF, &local->flags);
	vwrq->flags = IW_TXPOW_MWATT;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Retry limits
 */
static int airo_set_retry(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;
	int rc = -EINVAL;

	if(vwrq->disabled) {
		return -EINVAL;
	}
	readConfigRid(local, 1);
	if(vwrq->flags & IW_RETRY_LIMIT) {
		__le16 v = cpu_to_le16(vwrq->value);
		if(vwrq->flags & IW_RETRY_LONG)
			local->config.longRetryLimit = v;
		else if (vwrq->flags & IW_RETRY_SHORT)
			local->config.shortRetryLimit = v;
		else {
			/* No modifier : set both */
			local->config.longRetryLimit = v;
			local->config.shortRetryLimit = v;
		}
		set_bit (FLAG_COMMIT, &local->flags);
		rc = -EINPROGRESS;		/* Call commit handler */
	}
	if(vwrq->flags & IW_RETRY_LIFETIME) {
		local->config.txLifetime = cpu_to_le16(vwrq->value / 1024);
		set_bit (FLAG_COMMIT, &local->flags);
		rc = -EINPROGRESS;		/* Call commit handler */
	}
	return rc;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Retry limits
 */
static int airo_get_retry(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;

	vwrq->disabled = 0;      /* Can't be disabled */

	readConfigRid(local, 1);
	/* Note : by default, display the min retry number */
	if((vwrq->flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
		vwrq->flags = IW_RETRY_LIFETIME;
		vwrq->value = le16_to_cpu(local->config.txLifetime) * 1024;
	} else if((vwrq->flags & IW_RETRY_LONG)) {
		vwrq->flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
		vwrq->value = le16_to_cpu(local->config.longRetryLimit);
	} else {
		vwrq->flags = IW_RETRY_LIMIT;
		vwrq->value = le16_to_cpu(local->config.shortRetryLimit);
		if(local->config.shortRetryLimit != local->config.longRetryLimit)
			vwrq->flags |= IW_RETRY_SHORT;
	}

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get range info
 */
static int airo_get_range(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;
	struct iw_range *range = (struct iw_range *) extra;
	CapabilityRid cap_rid;		/* Card capability info */
	int		i;
	int		k;

	readCapabilityRid(local, &cap_rid, 1);

	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(*range));
	range->min_nwid = 0x0000;
	range->max_nwid = 0x0000;
	range->num_channels = 14;
	/* Should be based on cap_rid.country to give only
	 * what the current card support */
	k = 0;
	for(i = 0; i < 14; i++) {
		range->freq[k].i = i + 1; /* List index */
		range->freq[k].m = 100000 *
		     ieee80211_channel_to_frequency(i + 1, NL80211_BAND_2GHZ);
		range->freq[k++].e = 1;	/* Values in MHz -> * 10^5 * 10 */
	}
	range->num_frequency = k;

	range->sensitivity = 65535;

	/* Hum... Should put the right values there */
	if (local->rssi)
		range->max_qual.qual = 100;	/* % */
	else
		range->max_qual.qual = airo_get_max_quality(&cap_rid);
	range->max_qual.level = 0x100 - 120;	/* -120 dBm */
	range->max_qual.noise = 0x100 - 120;	/* -120 dBm */

	/* Experimental measurements - boundary 11/5.5 Mb/s */
	/* Note : with or without the (local->rssi), results
	 * are somewhat different. - Jean II */
	if (local->rssi) {
		range->avg_qual.qual = 50;		/* % */
		range->avg_qual.level = 0x100 - 70;	/* -70 dBm */
	} else {
		range->avg_qual.qual = airo_get_avg_quality(&cap_rid);
		range->avg_qual.level = 0x100 - 80;	/* -80 dBm */
	}
	range->avg_qual.noise = 0x100 - 85;		/* -85 dBm */

	for(i = 0 ; i < 8 ; i++) {
		range->bitrate[i] = cap_rid.supportedRates[i] * 500000;
		if(range->bitrate[i] == 0)
			break;
	}
	range->num_bitrates = i;

	/* Set an indication of the max TCP throughput
	 * in bit/s that we can expect using this interface.
	 * May be use for QoS stuff... Jean II */
	if(i > 2)
		range->throughput = 5000 * 1000;
	else
		range->throughput = 1500 * 1000;

	range->min_rts = 0;
	range->max_rts = AIRO_DEF_MTU;
	range->min_frag = 256;
	range->max_frag = AIRO_DEF_MTU;

	if(cap_rid.softCap & cpu_to_le16(2)) {
		// WEP: RC4 40 bits
		range->encoding_size[0] = 5;
		// RC4 ~128 bits
		if (cap_rid.softCap & cpu_to_le16(0x100)) {
			range->encoding_size[1] = 13;
			range->num_encoding_sizes = 2;
		} else
			range->num_encoding_sizes = 1;
		range->max_encoding_tokens =
			cap_rid.softCap & cpu_to_le16(0x80) ? 4 : 1;
	} else {
		range->num_encoding_sizes = 0;
		range->max_encoding_tokens = 0;
	}
	range->min_pmp = 0;
	range->max_pmp = 5000000;	/* 5 secs */
	range->min_pmt = 0;
	range->max_pmt = 65535 * 1024;	/* ??? */
	range->pmp_flags = IW_POWER_PERIOD;
	range->pmt_flags = IW_POWER_TIMEOUT;
	range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_ALL_R;

	/* Transmit Power - values are in mW */
	for(i = 0 ; i < 8 ; i++) {
		range->txpower[i] = le16_to_cpu(cap_rid.txPowerLevels[i]);
		if(range->txpower[i] == 0)
			break;
	}
	range->num_txpower = i;
	range->txpower_capa = IW_TXPOW_MWATT;
	range->we_version_source = 19;
	range->we_version_compiled = WIRELESS_EXT;
	range->retry_capa = IW_RETRY_LIMIT | IW_RETRY_LIFETIME;
	range->retry_flags = IW_RETRY_LIMIT;
	range->r_time_flags = IW_RETRY_LIFETIME;
	range->min_retry = 1;
	range->max_retry = 65535;
	range->min_r_time = 1024;
	range->max_r_time = 65535 * 1024;

	/* Event capability (kernel + driver) */
	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWTHRSPY) |
				IW_EVENT_CAPA_MASK(SIOCGIWAP) |
				IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;
	range->event_capa[4] = IW_EVENT_CAPA_MASK(IWEVTXDROP);
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Power Management
 */
static int airo_set_power(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	if (vwrq->disabled) {
		if (sniffing_mode(local))
			return -EINVAL;
		local->config.powerSaveMode = POWERSAVE_CAM;
		local->config.rmode &= ~RXMODE_MASK;
		local->config.rmode |= RXMODE_BC_MC_ADDR;
		set_bit (FLAG_COMMIT, &local->flags);
		return -EINPROGRESS;		/* Call commit handler */
	}
	if ((vwrq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		local->config.fastListenDelay = cpu_to_le16((vwrq->value + 500) / 1024);
		local->config.powerSaveMode = POWERSAVE_PSPCAM;
		set_bit (FLAG_COMMIT, &local->flags);
	} else if ((vwrq->flags & IW_POWER_TYPE) == IW_POWER_PERIOD) {
		local->config.fastListenInterval =
		local->config.listenInterval =
			cpu_to_le16((vwrq->value + 500) / 1024);
		local->config.powerSaveMode = POWERSAVE_PSPCAM;
		set_bit (FLAG_COMMIT, &local->flags);
	}
	switch (vwrq->flags & IW_POWER_MODE) {
		case IW_POWER_UNICAST_R:
			if (sniffing_mode(local))
				return -EINVAL;
			local->config.rmode &= ~RXMODE_MASK;
			local->config.rmode |= RXMODE_ADDR;
			set_bit (FLAG_COMMIT, &local->flags);
			break;
		case IW_POWER_ALL_R:
			if (sniffing_mode(local))
				return -EINVAL;
			local->config.rmode &= ~RXMODE_MASK;
			local->config.rmode |= RXMODE_BC_MC_ADDR;
			set_bit (FLAG_COMMIT, &local->flags);
		case IW_POWER_ON:
			/* This is broken, fixme ;-) */
			break;
		default:
			return -EINVAL;
	}
	// Note : we may want to factor local->need_commit here
	// Note2 : may also want to factor RXMODE_RFMON test
	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Power Management
 */
static int airo_get_power(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *vwrq,
			  char *extra)
{
	struct airo_info *local = dev->ml_priv;
	__le16 mode;

	readConfigRid(local, 1);
	mode = local->config.powerSaveMode;
	if ((vwrq->disabled = (mode == POWERSAVE_CAM)))
		return 0;
	if ((vwrq->flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		vwrq->value = le16_to_cpu(local->config.fastListenDelay) * 1024;
		vwrq->flags = IW_POWER_TIMEOUT;
	} else {
		vwrq->value = le16_to_cpu(local->config.fastListenInterval) * 1024;
		vwrq->flags = IW_POWER_PERIOD;
	}
	if ((local->config.rmode & RXMODE_MASK) == RXMODE_ADDR)
		vwrq->flags |= IW_POWER_UNICAST_R;
	else
		vwrq->flags |= IW_POWER_ALL_R;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : set Sensitivity
 */
static int airo_set_sens(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *vwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	local->config.rssiThreshold =
		cpu_to_le16(vwrq->disabled ? RSSI_DEFAULT : vwrq->value);
	set_bit (FLAG_COMMIT, &local->flags);

	return -EINPROGRESS;		/* Call commit handler */
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get Sensitivity
 */
static int airo_get_sens(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *vwrq,
			 char *extra)
{
	struct airo_info *local = dev->ml_priv;

	readConfigRid(local, 1);
	vwrq->value = le16_to_cpu(local->config.rssiThreshold);
	vwrq->disabled = (vwrq->value == 0);
	vwrq->fixed = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : get AP List
 * Note : this is deprecated in favor of IWSCAN
 */
static int airo_get_aplist(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *dwrq,
			   char *extra)
{
	struct airo_info *local = dev->ml_priv;
	struct sockaddr *address = (struct sockaddr *) extra;
	struct iw_quality *qual;
	BSSListRid BSSList;
	int i;
	int loseSync = capable(CAP_NET_ADMIN) ? 1: -1;

	qual = kmalloc_array(IW_MAX_AP, sizeof(*qual), GFP_KERNEL);
	if (!qual)
		return -ENOMEM;

	for (i = 0; i < IW_MAX_AP; i++) {
		u16 dBm;
		if (readBSSListRid(local, loseSync, &BSSList))
			break;
		loseSync = 0;
		memcpy(address[i].sa_data, BSSList.bssid, ETH_ALEN);
		address[i].sa_family = ARPHRD_ETHER;
		dBm = le16_to_cpu(BSSList.dBm);
		if (local->rssi) {
			qual[i].level = 0x100 - dBm;
			qual[i].qual = airo_dbm_to_pct(local->rssi, dBm);
			qual[i].updated = IW_QUAL_QUAL_UPDATED
					| IW_QUAL_LEVEL_UPDATED
					| IW_QUAL_DBM;
		} else {
			qual[i].level = (dBm + 321) / 2;
			qual[i].qual = 0;
			qual[i].updated = IW_QUAL_QUAL_INVALID
					| IW_QUAL_LEVEL_UPDATED
					| IW_QUAL_DBM;
		}
		qual[i].noise = local->wstats.qual.noise;
		if (BSSList.index == cpu_to_le16(0xffff))
			break;
	}
	if (!i) {
		StatusRid status_rid;		/* Card status info */
		readStatusRid(local, &status_rid, 1);
		for (i = 0;
		     i < min(IW_MAX_AP, 4) &&
			     (status_rid.bssid[i][0]
			      & status_rid.bssid[i][1]
			      & status_rid.bssid[i][2]
			      & status_rid.bssid[i][3]
			      & status_rid.bssid[i][4]
			      & status_rid.bssid[i][5])!=0xff &&
			     (status_rid.bssid[i][0]
			      | status_rid.bssid[i][1]
			      | status_rid.bssid[i][2]
			      | status_rid.bssid[i][3]
			      | status_rid.bssid[i][4]
			      | status_rid.bssid[i][5]);
		     i++) {
			memcpy(address[i].sa_data,
			       status_rid.bssid[i], ETH_ALEN);
			address[i].sa_family = ARPHRD_ETHER;
		}
	} else {
		dwrq->flags = 1; /* Should be define'd */
		memcpy(extra + sizeof(struct sockaddr) * i, qual,
		       sizeof(struct iw_quality) * i);
	}
	dwrq->length = i;

	kfree(qual);
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : Initiate Scan
 */
static int airo_set_scan(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *dwrq,
			 char *extra)
{
	struct airo_info *ai = dev->ml_priv;
	Cmd cmd;
	Resp rsp;
	int wake = 0;
	APListRid APList_rid_empty;

	/* Note : you may have realised that, as this is a SET operation,
	 * this is privileged and therefore a normal user can't
	 * perform scanning.
	 * This is not an error, while the device perform scanning,
	 * traffic doesn't flow, so it's a perfect DoS...
	 * Jean II */
	if (ai->flags & FLAG_RADIO_MASK) return -ENETDOWN;

	if (down_interruptible(&ai->sem))
		return -ERESTARTSYS;

	/* If there's already a scan in progress, don't
	 * trigger another one. */
	if (ai->scan_timeout > 0)
		goto out;

	/* Clear APList as it affects scan results */
	memset(&APList_rid_empty, 0, sizeof(APList_rid_empty));
	APList_rid_empty.len = cpu_to_le16(sizeof(APList_rid_empty));
	disable_MAC(ai, 2);
	writeAPListRid(ai, &APList_rid_empty, 0);
	enable_MAC(ai, 0);

	/* Initiate a scan command */
	ai->scan_timeout = RUN_AT(3*HZ);
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd=CMD_LISTBSS;
	issuecommand(ai, &cmd, &rsp);
	wake = 1;

out:
	up(&ai->sem);
	if (wake)
		wake_up_interruptible(&ai->thr_wait);
	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Translate scan data returned from the card to a card independent
 * format that the Wireless Tools will understand - Jean II
 */
static inline char *airo_translate_scan(struct net_device *dev,
					struct iw_request_info *info,
					char *current_ev,
					char *end_buf,
					BSSListRid *bss)
{
	struct airo_info *ai = dev->ml_priv;
	struct iw_event		iwe;		/* Temporary buffer */
	__le16			capabilities;
	char *			current_val;	/* For rates */
	int			i;
	char *		buf;
	u16 dBm;

	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_ADDR_LEN);

	/* Other entries will be displayed in the order we give them */

	/* Add the ESSID */
	iwe.u.data.length = bss->ssidLen;
	if(iwe.u.data.length > 32)
		iwe.u.data.length = 32;
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, bss->ssid);

	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	capabilities = bss->cap;
	if(capabilities & (CAP_ESS | CAP_IBSS)) {
		if(capabilities & CAP_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_UINT_LEN);
	}

	/* Add frequency */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = le16_to_cpu(bss->dsChannel);
	iwe.u.freq.m = 100000 *
	      ieee80211_channel_to_frequency(iwe.u.freq.m, NL80211_BAND_2GHZ);
	iwe.u.freq.e = 1;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_FREQ_LEN);

	dBm = le16_to_cpu(bss->dBm);

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	if (ai->rssi) {
		iwe.u.qual.level = 0x100 - dBm;
		iwe.u.qual.qual = airo_dbm_to_pct(ai->rssi, dBm);
		iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED
				| IW_QUAL_LEVEL_UPDATED
				| IW_QUAL_DBM;
	} else {
		iwe.u.qual.level = (dBm + 321) / 2;
		iwe.u.qual.qual = 0;
		iwe.u.qual.updated = IW_QUAL_QUAL_INVALID
				| IW_QUAL_LEVEL_UPDATED
				| IW_QUAL_DBM;
	}
	iwe.u.qual.noise = ai->wstats.qual.noise;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if(capabilities & CAP_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, bss->ssid);

	/* Rate : stuffing multiple values in a single event require a bit
	 * more of magic - Jean II */
	current_val = current_ev + iwe_stream_lcp_len(info);

	iwe.cmd = SIOCGIWRATE;
	/* Those two flags are ignored... */
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
	/* Max 8 values */
	for(i = 0 ; i < 8 ; i++) {
		/* NULL terminated */
		if(bss->rates[i] == 0)
			break;
		/* Bit rate given in 500 kb/s units (+ 0x80) */
		iwe.u.bitrate.value = ((bss->rates[i] & 0x7f) * 500000);
		/* Add new value to event */
		current_val = iwe_stream_add_value(info, current_ev,
						   current_val, end_buf,
						   &iwe, IW_EV_PARAM_LEN);
	}
	/* Check if we added any event */
	if ((current_val - current_ev) > iwe_stream_lcp_len(info))
		current_ev = current_val;

	/* Beacon interval */
	buf = kmalloc(30, GFP_KERNEL);
	if (buf) {
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, "bcn_int=%d", bss->beaconInterval);
		iwe.u.data.length = strlen(buf);
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, buf);
		kfree(buf);
	}

	/* Put WPA/RSN Information Elements into the event stream */
	if (test_bit(FLAG_WPA_CAPABLE, &ai->flags)) {
		unsigned int num_null_ies = 0;
		u16 length = sizeof (bss->extra.iep);
		u8 *ie = (void *)&bss->extra.iep;

		while ((length >= 2) && (num_null_ies < 2)) {
			if (2 + ie[1] > length) {
				/* Invalid element, don't continue parsing IE */
				break;
			}

			switch (ie[0]) {
			case WLAN_EID_SSID:
				/* Two zero-length SSID elements
				 * mean we're done parsing elements */
				if (!ie[1])
					num_null_ies++;
				break;

			case WLAN_EID_VENDOR_SPECIFIC:
				if (ie[1] >= 4 &&
				    ie[2] == 0x00 &&
				    ie[3] == 0x50 &&
				    ie[4] == 0xf2 &&
				    ie[5] == 0x01) {
					iwe.cmd = IWEVGENIE;
					/* 64 is an arbitrary cut-off */
					iwe.u.data.length = min(ie[1] + 2,
								64);
					current_ev = iwe_stream_add_point(
							info, current_ev,
							end_buf, &iwe, ie);
				}
				break;

			case WLAN_EID_RSN:
				iwe.cmd = IWEVGENIE;
				/* 64 is an arbitrary cut-off */
				iwe.u.data.length = min(ie[1] + 2, 64);
				current_ev = iwe_stream_add_point(
					info, current_ev, end_buf,
					&iwe, ie);
				break;

			default:
				break;
			}

			length -= 2 + ie[1];
			ie += 2 + ie[1];
		}
	}
	return current_ev;
}

/*------------------------------------------------------------------*/
/*
 * Wireless Handler : Read Scan Results
 */
static int airo_get_scan(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_point *dwrq,
			 char *extra)
{
	struct airo_info *ai = dev->ml_priv;
	BSSListElement *net;
	int err = 0;
	char *current_ev = extra;

	/* If a scan is in-progress, return -EAGAIN */
	if (ai->scan_timeout > 0)
		return -EAGAIN;

	if (down_interruptible(&ai->sem))
		return -EAGAIN;

	list_for_each_entry (net, &ai->network_list, list) {
		/* Translate to WE format this entry */
		current_ev = airo_translate_scan(dev, info, current_ev,
						 extra + dwrq->length,
						 &net->bss);

		/* Check if there is space for one more entry */
		if((extra + dwrq->length - current_ev) <= IW_EV_ADDR_LEN) {
			/* Ask user space to try again with a bigger buffer */
			err = -E2BIG;
			goto out;
		}
	}

	/* Length of data */
	dwrq->length = (current_ev - extra);
	dwrq->flags = 0;	/* todo */

out:
	up(&ai->sem);
	return err;
}

/*------------------------------------------------------------------*/
/*
 * Commit handler : called after a bunch of SET operations
 */
static int airo_config_commit(struct net_device *dev,
			      struct iw_request_info *info,	/* NULL */
			      void *zwrq,			/* NULL */
			      char *extra)			/* NULL */
{
	struct airo_info *local = dev->ml_priv;

	if (!test_bit (FLAG_COMMIT, &local->flags))
		return 0;

	/* Some of the "SET" function may have modified some of the
	 * parameters. It's now time to commit them in the card */
	disable_MAC(local, 1);
	if (test_bit (FLAG_RESET, &local->flags)) {
		SsidRid SSID_rid;

		readSsidRid(local, &SSID_rid);
		if (test_bit(FLAG_MPI,&local->flags))
			setup_card(local, dev->dev_addr, 1 );
		else
			reset_airo_card(dev);
		disable_MAC(local, 1);
		writeSsidRid(local, &SSID_rid, 1);
		writeAPListRid(local, &local->APList, 1);
	}
	if (down_interruptible(&local->sem))
		return -ERESTARTSYS;
	writeConfigRid(local, 0);
	enable_MAC(local, 0);
	if (test_bit (FLAG_RESET, &local->flags))
		airo_set_promisc(local);
	else
		up(&local->sem);

	return 0;
}

/*------------------------------------------------------------------*/
/*
 * Structures to export the Wireless Handlers
 */

static const struct iw_priv_args airo_private_args[] = {
/*{ cmd,         set_args,                            get_args, name } */
  { AIROIOCTL, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | sizeof (aironet_ioctl),
    IW_PRIV_TYPE_BYTE | 2047, "airoioctl" },
  { AIROIDIFC, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | sizeof (aironet_ioctl),
    IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "airoidifc" },
};

static const iw_handler		airo_handler[] =
{
	(iw_handler) airo_config_commit,	/* SIOCSIWCOMMIT */
	(iw_handler) airo_get_name,		/* SIOCGIWNAME */
	(iw_handler) NULL,			/* SIOCSIWNWID */
	(iw_handler) NULL,			/* SIOCGIWNWID */
	(iw_handler) airo_set_freq,		/* SIOCSIWFREQ */
	(iw_handler) airo_get_freq,		/* SIOCGIWFREQ */
	(iw_handler) airo_set_mode,		/* SIOCSIWMODE */
	(iw_handler) airo_get_mode,		/* SIOCGIWMODE */
	(iw_handler) airo_set_sens,		/* SIOCSIWSENS */
	(iw_handler) airo_get_sens,		/* SIOCGIWSENS */
	(iw_handler) NULL,			/* SIOCSIWRANGE */
	(iw_handler) airo_get_range,		/* SIOCGIWRANGE */
	(iw_handler) NULL,			/* SIOCSIWPRIV */
	(iw_handler) NULL,			/* SIOCGIWPRIV */
	(iw_handler) NULL,			/* SIOCSIWSTATS */
	(iw_handler) NULL,			/* SIOCGIWSTATS */
	iw_handler_set_spy,			/* SIOCSIWSPY */
	iw_handler_get_spy,			/* SIOCGIWSPY */
	iw_handler_set_thrspy,			/* SIOCSIWTHRSPY */
	iw_handler_get_thrspy,			/* SIOCGIWTHRSPY */
	(iw_handler) airo_set_wap,		/* SIOCSIWAP */
	(iw_handler) airo_get_wap,		/* SIOCGIWAP */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) airo_get_aplist,		/* SIOCGIWAPLIST */
	(iw_handler) airo_set_scan,		/* SIOCSIWSCAN */
	(iw_handler) airo_get_scan,		/* SIOCGIWSCAN */
	(iw_handler) airo_set_essid,		/* SIOCSIWESSID */
	(iw_handler) airo_get_essid,		/* SIOCGIWESSID */
	(iw_handler) airo_set_nick,		/* SIOCSIWNICKN */
	(iw_handler) airo_get_nick,		/* SIOCGIWNICKN */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) airo_set_rate,		/* SIOCSIWRATE */
	(iw_handler) airo_get_rate,		/* SIOCGIWRATE */
	(iw_handler) airo_set_rts,		/* SIOCSIWRTS */
	(iw_handler) airo_get_rts,		/* SIOCGIWRTS */
	(iw_handler) airo_set_frag,		/* SIOCSIWFRAG */
	(iw_handler) airo_get_frag,		/* SIOCGIWFRAG */
	(iw_handler) airo_set_txpow,		/* SIOCSIWTXPOW */
	(iw_handler) airo_get_txpow,		/* SIOCGIWTXPOW */
	(iw_handler) airo_set_retry,		/* SIOCSIWRETRY */
	(iw_handler) airo_get_retry,		/* SIOCGIWRETRY */
	(iw_handler) airo_set_encode,		/* SIOCSIWENCODE */
	(iw_handler) airo_get_encode,		/* SIOCGIWENCODE */
	(iw_handler) airo_set_power,		/* SIOCSIWPOWER */
	(iw_handler) airo_get_power,		/* SIOCGIWPOWER */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* -- hole -- */
	(iw_handler) NULL,			/* SIOCSIWGENIE */
	(iw_handler) NULL,			/* SIOCGIWGENIE */
	(iw_handler) airo_set_auth,		/* SIOCSIWAUTH */
	(iw_handler) airo_get_auth,		/* SIOCGIWAUTH */
	(iw_handler) airo_set_encodeext,	/* SIOCSIWENCODEEXT */
	(iw_handler) airo_get_encodeext,	/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,			/* SIOCSIWPMKSA */
};

/* Note : don't describe AIROIDIFC and AIROOLDIDIFC in here.
 * We want to force the use of the ioctl code, because those can't be
 * won't work the iw_handler code (because they simultaneously read
 * and write data and iw_handler can't do that).
 * Note that it's perfectly legal to read/write on a single ioctl command,
 * you just can't use iwpriv and need to force it via the ioctl handler.
 * Jean II */
static const iw_handler		airo_private_handler[] =
{
	NULL,				/* SIOCIWFIRSTPRIV */
};

static const struct iw_handler_def	airo_handler_def =
{
	.num_standard	= ARRAY_SIZE(airo_handler),
	.num_private	= ARRAY_SIZE(airo_private_handler),
	.num_private_args = ARRAY_SIZE(airo_private_args),
	.standard	= airo_handler,
	.private	= airo_private_handler,
	.private_args	= airo_private_args,
	.get_wireless_stats = airo_get_wireless_stats,
};

/*
 * This defines the configuration part of the Wireless Extensions
 * Note : irq and spinlock protection will occur in the subroutines
 *
 * TODO :
 *	o Check input value more carefully and fill correct values in range
 *	o Test and shakeout the bugs (if any)
 *
 * Jean II
 *
 * Javier Achirica did a great job of merging code from the unnamed CISCO
 * developer that added support for flashing the card.
 */
static int airo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int rc = 0;
	struct airo_info *ai = dev->ml_priv;

	if (ai->power.event)
		return 0;

	switch (cmd) {
#ifdef CISCO_EXT
	case AIROIDIFC:
#ifdef AIROOLDIDIFC
	case AIROOLDIDIFC:
#endif
	{
		int val = AIROMAGIC;
		aironet_ioctl com;
		if (copy_from_user(&com,rq->ifr_data,sizeof(com)))
			rc = -EFAULT;
		else if (copy_to_user(com.data,(char *)&val,sizeof(val)))
			rc = -EFAULT;
	}
	break;

	case AIROIOCTL:
#ifdef AIROOLDIOCTL
	case AIROOLDIOCTL:
#endif
		/* Get the command struct and hand it off for evaluation by
		 * the proper subfunction
		 */
	{
		aironet_ioctl com;
		if (copy_from_user(&com,rq->ifr_data,sizeof(com))) {
			rc = -EFAULT;
			break;
		}

		/* Separate R/W functions bracket legality here
		 */
		if ( com.command == AIRORSWVERSION ) {
			if (copy_to_user(com.data, swversion, sizeof(swversion)))
				rc = -EFAULT;
			else
				rc = 0;
		}
		else if ( com.command <= AIRORRID)
			rc = readrids(dev,&com);
		else if ( com.command >= AIROPCAP && com.command <= (AIROPLEAPUSR+2) )
			rc = writerids(dev,&com);
		else if ( com.command >= AIROFLSHRST && com.command <= AIRORESTART )
			rc = flashcard(dev,&com);
		else
			rc = -EINVAL;      /* Bad command in ioctl */
	}
	break;
#endif /* CISCO_EXT */

	// All other calls are currently unsupported
	default:
		rc = -EOPNOTSUPP;
	}
	return rc;
}

/*
 * Get the Wireless stats out of the driver
 * Note : irq and spinlock protection will occur in the subroutines
 *
 * TODO :
 *	o Check if work in Ad-Hoc mode (otherwise, use SPY, as in wvlan_cs)
 *
 * Jean
 */
static void airo_read_wireless_stats(struct airo_info *local)
{
	StatusRid status_rid;
	StatsRid stats_rid;
	CapabilityRid cap_rid;
	__le32 *vals = stats_rid.vals;

	/* Get stats out of the card */
	clear_bit(JOB_WSTATS, &local->jobs);
	if (local->power.event) {
		up(&local->sem);
		return;
	}
	readCapabilityRid(local, &cap_rid, 0);
	readStatusRid(local, &status_rid, 0);
	readStatsRid(local, &stats_rid, RID_STATS, 0);
	up(&local->sem);

	/* The status */
	local->wstats.status = le16_to_cpu(status_rid.mode);

	/* Signal quality and co */
	if (local->rssi) {
		local->wstats.qual.level =
			airo_rssi_to_dbm(local->rssi,
					 le16_to_cpu(status_rid.sigQuality));
		/* normalizedSignalStrength appears to be a percentage */
		local->wstats.qual.qual =
			le16_to_cpu(status_rid.normalizedSignalStrength);
	} else {
		local->wstats.qual.level =
			(le16_to_cpu(status_rid.normalizedSignalStrength) + 321) / 2;
		local->wstats.qual.qual = airo_get_quality(&status_rid, &cap_rid);
	}
	if (le16_to_cpu(status_rid.len) >= 124) {
		local->wstats.qual.noise = 0x100 - status_rid.noisedBm;
		local->wstats.qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	} else {
		local->wstats.qual.noise = 0;
		local->wstats.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID | IW_QUAL_DBM;
	}

	/* Packets discarded in the wireless adapter due to wireless
	 * specific problems */
	local->wstats.discard.nwid = le32_to_cpu(vals[56]) +
				     le32_to_cpu(vals[57]) +
				     le32_to_cpu(vals[58]); /* SSID Mismatch */
	local->wstats.discard.code = le32_to_cpu(vals[6]);/* RxWepErr */
	local->wstats.discard.fragment = le32_to_cpu(vals[30]);
	local->wstats.discard.retries = le32_to_cpu(vals[10]);
	local->wstats.discard.misc = le32_to_cpu(vals[1]) +
				     le32_to_cpu(vals[32]);
	local->wstats.miss.beacon = le32_to_cpu(vals[34]);
}

static struct iw_statistics *airo_get_wireless_stats(struct net_device *dev)
{
	struct airo_info *local =  dev->ml_priv;

	if (!test_bit(JOB_WSTATS, &local->jobs)) {
		/* Get stats out of the card if available */
		if (down_trylock(&local->sem) != 0) {
			set_bit(JOB_WSTATS, &local->jobs);
			wake_up_interruptible(&local->thr_wait);
		} else
			airo_read_wireless_stats(local);
	}

	return &local->wstats;
}

#ifdef CISCO_EXT
/*
 * This just translates from driver IOCTL codes to the command codes to
 * feed to the radio's host interface. Things can be added/deleted
 * as needed.  This represents the READ side of control I/O to
 * the card
 */
static int readrids(struct net_device *dev, aironet_ioctl *comp) {
	unsigned short ridcode;
	unsigned char *iobuf;
	int len;
	struct airo_info *ai = dev->ml_priv;

	if (test_bit(FLAG_FLASHING, &ai->flags))
		return -EIO;

	switch(comp->command)
	{
	case AIROGCAP:      ridcode = RID_CAPABILITIES; break;
	case AIROGCFG:      ridcode = RID_CONFIG;
		if (test_bit(FLAG_COMMIT, &ai->flags)) {
			disable_MAC (ai, 1);
			writeConfigRid (ai, 1);
			enable_MAC(ai, 1);
		}
		break;
	case AIROGSLIST:    ridcode = RID_SSID;         break;
	case AIROGVLIST:    ridcode = RID_APLIST;       break;
	case AIROGDRVNAM:   ridcode = RID_DRVNAME;      break;
	case AIROGEHTENC:   ridcode = RID_ETHERENCAP;   break;
	case AIROGWEPKTMP:  ridcode = RID_WEP_TEMP;
		/* Only super-user can read WEP keys */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		break;
	case AIROGWEPKNV:   ridcode = RID_WEP_PERM;
		/* Only super-user can read WEP keys */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		break;
	case AIROGSTAT:     ridcode = RID_STATUS;       break;
	case AIROGSTATSD32: ridcode = RID_STATSDELTA;   break;
	case AIROGSTATSC32: ridcode = RID_STATS;        break;
	case AIROGMICSTATS:
		if (copy_to_user(comp->data, &ai->micstats,
				 min((int)comp->len,(int)sizeof(ai->micstats))))
			return -EFAULT;
		return 0;
	case AIRORRID:      ridcode = comp->ridnum;     break;
	default:
		return -EINVAL;
	}

	if ((iobuf = kmalloc(RIDSIZE, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	PC4500_readrid(ai,ridcode,iobuf,RIDSIZE, 1);
	/* get the count of bytes in the rid  docs say 1st 2 bytes is it.
	 * then return it to the user
	 * 9/22/2000 Honor user given length
	 */
	len = comp->len;

	if (copy_to_user(comp->data, iobuf, min(len, (int)RIDSIZE))) {
		kfree (iobuf);
		return -EFAULT;
	}
	kfree (iobuf);
	return 0;
}

/*
 * Danger Will Robinson write the rids here
 */

static int writerids(struct net_device *dev, aironet_ioctl *comp) {
	struct airo_info *ai = dev->ml_priv;
	int  ridcode;
        int  enabled;
	int (*writer)(struct airo_info *, u16 rid, const void *, int, int);
	unsigned char *iobuf;

	/* Only super-user can write RIDs */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (test_bit(FLAG_FLASHING, &ai->flags))
		return -EIO;

	ridcode = 0;
	writer = do_writerid;

	switch(comp->command)
	{
	case AIROPSIDS:     ridcode = RID_SSID;         break;
	case AIROPCAP:      ridcode = RID_CAPABILITIES; break;
	case AIROPAPLIST:   ridcode = RID_APLIST;       break;
	case AIROPCFG: ai->config.len = 0;
			    clear_bit(FLAG_COMMIT, &ai->flags);
			    ridcode = RID_CONFIG;       break;
	case AIROPWEPKEYNV: ridcode = RID_WEP_PERM;     break;
	case AIROPLEAPUSR:  ridcode = RID_LEAPUSERNAME; break;
	case AIROPLEAPPWD:  ridcode = RID_LEAPPASSWORD; break;
	case AIROPWEPKEY:   ridcode = RID_WEP_TEMP; writer = PC4500_writerid;
		break;
	case AIROPLEAPUSR+1: ridcode = 0xFF2A;          break;
	case AIROPLEAPUSR+2: ridcode = 0xFF2B;          break;

		/* this is not really a rid but a command given to the card
		 * same with MAC off
		 */
	case AIROPMACON:
		if (enable_MAC(ai, 1) != 0)
			return -EIO;
		return 0;

		/*
		 * Evidently this code in the airo driver does not get a symbol
		 * as disable_MAC. it's probably so short the compiler does not gen one.
		 */
	case AIROPMACOFF:
		disable_MAC(ai, 1);
		return 0;

		/* This command merely clears the counts does not actually store any data
		 * only reads rid. But as it changes the cards state, I put it in the
		 * writerid routines.
		 */
	case AIROPSTCLR:
		if ((iobuf = kmalloc(RIDSIZE, GFP_KERNEL)) == NULL)
			return -ENOMEM;

		PC4500_readrid(ai,RID_STATSDELTACLEAR,iobuf,RIDSIZE, 1);

		enabled = ai->micstats.enabled;
		memset(&ai->micstats,0,sizeof(ai->micstats));
		ai->micstats.enabled = enabled;

		if (copy_to_user(comp->data, iobuf,
				 min((int)comp->len, (int)RIDSIZE))) {
			kfree (iobuf);
			return -EFAULT;
		}
		kfree (iobuf);
		return 0;

	default:
		return -EOPNOTSUPP;	/* Blarg! */
	}
	if(comp->len > RIDSIZE)
		return -EINVAL;

	if ((iobuf = kmalloc(RIDSIZE, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	if (copy_from_user(iobuf,comp->data,comp->len)) {
		kfree (iobuf);
		return -EFAULT;
	}

	if (comp->command == AIROPCFG) {
		ConfigRid *cfg = (ConfigRid *)iobuf;

		if (test_bit(FLAG_MIC_CAPABLE, &ai->flags))
			cfg->opmode |= MODE_MIC;

		if ((cfg->opmode & MODE_CFG_MASK) == MODE_STA_IBSS)
			set_bit (FLAG_ADHOC, &ai->flags);
		else
			clear_bit (FLAG_ADHOC, &ai->flags);
	}

	if((*writer)(ai, ridcode, iobuf,comp->len,1)) {
		kfree (iobuf);
		return -EIO;
	}
	kfree (iobuf);
	return 0;
}

/*****************************************************************************
 * Ancillary flash / mod functions much black magic lurkes here              *
 *****************************************************************************
 */

/*
 * Flash command switch table
 */

static int flashcard(struct net_device *dev, aironet_ioctl *comp) {
	int z;

	/* Only super-user can modify flash */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch(comp->command)
	{
	case AIROFLSHRST:
		return cmdreset((struct airo_info *)dev->ml_priv);

	case AIROFLSHSTFL:
		if (!AIRO_FLASH(dev) &&
		    (AIRO_FLASH(dev) = kmalloc(FLASHSIZE, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		return setflashmode((struct airo_info *)dev->ml_priv);

	case AIROFLSHGCHR: /* Get char from aux */
		if(comp->len != sizeof(int))
			return -EINVAL;
		if (copy_from_user(&z,comp->data,comp->len))
			return -EFAULT;
		return flashgchar((struct airo_info *)dev->ml_priv, z, 8000);

	case AIROFLSHPCHR: /* Send char to card. */
		if(comp->len != sizeof(int))
			return -EINVAL;
		if (copy_from_user(&z,comp->data,comp->len))
			return -EFAULT;
		return flashpchar((struct airo_info *)dev->ml_priv, z, 8000);

	case AIROFLPUTBUF: /* Send 32k to card */
		if (!AIRO_FLASH(dev))
			return -ENOMEM;
		if(comp->len > FLASHSIZE)
			return -EINVAL;
		if (copy_from_user(AIRO_FLASH(dev), comp->data, comp->len))
			return -EFAULT;

		flashputbuf((struct airo_info *)dev->ml_priv);
		return 0;

	case AIRORESTART:
		if (flashrestart((struct airo_info *)dev->ml_priv, dev))
			return -EIO;
		return 0;
	}
	return -EINVAL;
}

#define FLASH_COMMAND  0x7e7e

/*
 * STEP 1)
 * Disable MAC and do soft reset on
 * card.
 */

static int cmdreset(struct airo_info *ai) {
	disable_MAC(ai, 1);

	if(!waitbusy (ai)){
		airo_print_info(ai->dev->name, "Waitbusy hang before RESET");
		return -EBUSY;
	}

	OUT4500(ai,COMMAND,CMD_SOFTRESET);

	ssleep(1);			/* WAS 600 12/7/00 */

	if(!waitbusy (ai)){
		airo_print_info(ai->dev->name, "Waitbusy hang AFTER RESET");
		return -EBUSY;
	}
	return 0;
}

/* STEP 2)
 * Put the card in legendary flash
 * mode
 */

static int setflashmode (struct airo_info *ai) {
	set_bit (FLAG_FLASHING, &ai->flags);

	OUT4500(ai, SWS0, FLASH_COMMAND);
	OUT4500(ai, SWS1, FLASH_COMMAND);
	if (probe) {
		OUT4500(ai, SWS0, FLASH_COMMAND);
		OUT4500(ai, COMMAND,0x10);
	} else {
		OUT4500(ai, SWS2, FLASH_COMMAND);
		OUT4500(ai, SWS3, FLASH_COMMAND);
		OUT4500(ai, COMMAND,0);
	}
	msleep(500);		/* 500ms delay */

	if(!waitbusy(ai)) {
		clear_bit (FLAG_FLASHING, &ai->flags);
		airo_print_info(ai->dev->name, "Waitbusy hang after setflash mode");
		return -EIO;
	}
	return 0;
}

/* Put character to SWS0 wait for dwelltime
 * x 50us for  echo .
 */

static int flashpchar(struct airo_info *ai,int byte,int dwelltime) {
	int echo;
	int waittime;

	byte |= 0x8000;

	if(dwelltime == 0 )
		dwelltime = 200;

	waittime=dwelltime;

	/* Wait for busy bit d15 to go false indicating buffer empty */
	while ((IN4500 (ai, SWS0) & 0x8000) && waittime > 0) {
		udelay (50);
		waittime -= 50;
	}

	/* timeout for busy clear wait */
	if(waittime <= 0 ){
		airo_print_info(ai->dev->name, "flash putchar busywait timeout!");
		return -EBUSY;
	}

	/* Port is clear now write byte and wait for it to echo back */
	do {
		OUT4500(ai,SWS0,byte);
		udelay(50);
		dwelltime -= 50;
		echo = IN4500(ai,SWS1);
	} while (dwelltime >= 0 && echo != byte);

	OUT4500(ai,SWS1,0);

	return (echo == byte) ? 0 : -EIO;
}

/*
 * Get a character from the card matching matchbyte
 * Step 3)
 */
static int flashgchar(struct airo_info *ai,int matchbyte,int dwelltime){
	int           rchar;
	unsigned char rbyte=0;

	do {
		rchar = IN4500(ai,SWS1);

		if(dwelltime && !(0x8000 & rchar)){
			dwelltime -= 10;
			mdelay(10);
			continue;
		}
		rbyte = 0xff & rchar;

		if( (rbyte == matchbyte) && (0x8000 & rchar) ){
			OUT4500(ai,SWS1,0);
			return 0;
		}
		if( rbyte == 0x81 || rbyte == 0x82 || rbyte == 0x83 || rbyte == 0x1a || 0xffff == rchar)
			break;
		OUT4500(ai,SWS1,0);

	}while(dwelltime > 0);
	return -EIO;
}

/*
 * Transfer 32k of firmware data from user buffer to our buffer and
 * send to the card
 */

static int flashputbuf(struct airo_info *ai){
	int            nwords;

	/* Write stuff */
	if (test_bit(FLAG_MPI,&ai->flags))
		memcpy_toio(ai->pciaux + 0x8000, ai->flash, FLASHSIZE);
	else {
		OUT4500(ai,AUXPAGE,0x100);
		OUT4500(ai,AUXOFF,0);

		for(nwords=0;nwords != FLASHSIZE / 2;nwords++){
			OUT4500(ai,AUXDATA,ai->flash[nwords] & 0xffff);
		}
	}
	OUT4500(ai,SWS0,0x8000);

	return 0;
}

/*
 *
 */
static int flashrestart(struct airo_info *ai,struct net_device *dev){
	int    i,status;

	ssleep(1);			/* Added 12/7/00 */
	clear_bit (FLAG_FLASHING, &ai->flags);
	if (test_bit(FLAG_MPI, &ai->flags)) {
		status = mpi_init_descriptors(ai);
		if (status != SUCCESS)
			return status;
	}
	status = setup_card(ai, dev->dev_addr, 1);

	if (!test_bit(FLAG_MPI,&ai->flags))
		for( i = 0; i < MAX_FIDS; i++ ) {
			ai->fids[i] = transmit_allocate
				( ai, AIRO_DEF_MTU, i >= MAX_FIDS / 2 );
		}

	ssleep(1);			/* Added 12/7/00 */
	return status;
}
#endif /* CISCO_EXT */

/*
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    In addition:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. The name of the author may not be used to endorse or promote
       products derived from this software without specific prior written
       permission.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

module_init(airo_init_module);
module_exit(airo_cleanup_module);
