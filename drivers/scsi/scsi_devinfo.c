
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <scsi/scsi_device.h>
#include <scsi/scsi_devinfo.h>

#include "scsi_priv.h"


/*
 * scsi_dev_info_list: structure to hold black/white listed devices.
 */
struct scsi_dev_info_list {
	struct list_head dev_info_list;
	char vendor[8];
	char model[16];
	unsigned flags;
	unsigned compatible; /* for use with scsi_static_device_list entries */
};

struct scsi_dev_info_list_table {
	struct list_head node;	/* our node for being on the master list */
	struct list_head scsi_dev_info_list; /* head of dev info list */
	const char *name;	/* name of list for /proc (NULL for global) */
	int key;		/* unique numeric identifier */
};


static const char spaces[] = "                "; /* 16 of them */
static unsigned scsi_default_dev_flags;
static LIST_HEAD(scsi_dev_info_list);
static char scsi_dev_flags[256];

/*
 * scsi_static_device_list: deprecated list of devices that require
 * settings that differ from the default, includes black-listed (broken)
 * devices. The entries here are added to the tail of scsi_dev_info_list
 * via scsi_dev_info_list_init.
 *
 * Do not add to this list, use the command line or proc interface to add
 * to the scsi_dev_info_list. This table will eventually go away.
 */
static struct {
	char *vendor;
	char *model;
	char *revision;	/* revision known to be bad, unused */
	unsigned flags;
} scsi_static_device_list[] __initdata = {
	/*
	 * The following devices are known not to tolerate a lun != 0 scan
	 * for one reason or another. Some will respond to all luns,
	 * others will lock up.
	 */
	{"Aashima", "IMAGERY 2400SP", "1.03", BLIST_NOLUN},	/* locks up */
	{"CHINON", "CD-ROM CDS-431", "H42", BLIST_NOLUN},	/* locks up */
	{"CHINON", "CD-ROM CDS-535", "Q14", BLIST_NOLUN},	/* locks up */
	{"DENON", "DRD-25X", "V", BLIST_NOLUN},			/* locks up */
	{"HITACHI", "DK312C", "CM81", BLIST_NOLUN},	/* responds to all lun */
	{"HITACHI", "DK314C", "CR21", BLIST_NOLUN},	/* responds to all lun */
	{"IBM", "2104-DU3", NULL, BLIST_NOLUN},		/* locks up */
	{"IBM", "2104-TU3", NULL, BLIST_NOLUN},		/* locks up */
	{"IMS", "CDD521/10", "2.06", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-3280", "PR02", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-4380S", "B3C", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "MXT-1240S", "I1.2", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-4170S", "B5A", BLIST_NOLUN},	/* locks up */
	{"MAXTOR", "XT-8760S", "B7B", BLIST_NOLUN},	/* locks up */
	{"MEDIAVIS", "RENO CD-ROMX2A", "2.03", BLIST_NOLUN},	/* responds to all lun */
	{"MICROTEK", "ScanMakerIII", "2.30", BLIST_NOLUN},	/* responds to all lun */
	{"NEC", "CD-ROM DRIVE:841", "1.0", BLIST_NOLUN},/* locks up */
	{"PHILIPS", "PCA80SC", "V4-2", BLIST_NOLUN},	/* responds to all lun */
	{"RODIME", "RO3000S", "2.33", BLIST_NOLUN},	/* locks up */
	{"SUN", "SENA", NULL, BLIST_NOLUN},		/* responds to all luns */
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * aha152x controller, which causes SCSI code to reset bus.
	 */
	{"SANYO", "CRD-250S", "1.20", BLIST_NOLUN},
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * aha152x controller, which causes SCSI code to reset bus.
	 */
	{"SEAGATE", "ST157N", "\004|j", BLIST_NOLUN},
	{"SEAGATE", "ST296", "921", BLIST_NOLUN},	/* responds to all lun */
	{"SEAGATE", "ST1581", "6538", BLIST_NOLUN},	/* responds to all lun */
	{"SONY", "CD-ROM CDU-541", "4.3d", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-55S", "1.0i", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-561", "1.7x", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-8012", NULL, BLIST_NOLUN},
	{"SONY", "SDT-5000", "3.17", BLIST_SELECT_NO_ATN},
	{"TANDBERG", "TDC 3600", "U07", BLIST_NOLUN},	/* locks up */
	{"TEAC", "CD-R55S", "1.0H", BLIST_NOLUN},	/* locks up */
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * seagate controller, which causes SCSI code to reset bus.
	 */
	{"TEAC", "CD-ROM", "1.06", BLIST_NOLUN},
	{"TEAC", "MT-2ST/45S2-27", "RV M", BLIST_NOLUN},	/* responds to all lun */
	/*
	 * The following causes a failed REQUEST SENSE on lun 1 for
	 * seagate controller, which causes SCSI code to reset bus.
	 */
	{"HP", "C1750A", "3226", BLIST_NOLUN},		/* scanjet iic */
	{"HP", "C1790A", "", BLIST_NOLUN},		/* scanjet iip */
	{"HP", "C2500A", "", BLIST_NOLUN},		/* scanjet iicx */
	{"MEDIAVIS", "CDR-H93MV", "1.31", BLIST_NOLUN},	/* locks up */
	{"MICROTEK", "ScanMaker II", "5.61", BLIST_NOLUN},	/* responds to all lun */
	{"MITSUMI", "CD-R CR-2201CS", "6119", BLIST_NOLUN},	/* locks up */
	{"NEC", "D3856", "0009", BLIST_NOLUN},
	{"QUANTUM", "LPS525S", "3110", BLIST_NOLUN},	/* locks up */
	{"QUANTUM", "PD1225S", "3110", BLIST_NOLUN},	/* locks up */
	{"QUANTUM", "FIREBALL ST4.3S", "0F0C", BLIST_NOLUN},	/* locks up */
	{"RELISYS", "Scorpio", NULL, BLIST_NOLUN},	/* responds to all lun */
	{"SANKYO", "CP525", "6.64", BLIST_NOLUN},	/* causes failed REQ SENSE, extra reset */
	{"TEXEL", "CD-ROM", "1.06", BLIST_NOLUN},
	{"transtec", "T5008", "0001", BLIST_NOREPORTLUN },
	{"YAMAHA", "CDR100", "1.00", BLIST_NOLUN},	/* locks up */
	{"YAMAHA", "CDR102", "1.00", BLIST_NOLUN},	/* locks up */
	{"YAMAHA", "CRW8424S", "1.0", BLIST_NOLUN},	/* locks up */
	{"YAMAHA", "CRW6416S", "1.0c", BLIST_NOLUN},	/* locks up */
	{"", "Scanner", "1.80", BLIST_NOLUN},	/* responds to all lun */

	/*
	 * Other types of devices that have special flags.
	 * Note that all USB devices should have the BLIST_INQUIRY_36 flag.
	 */
	{"3PARdata", "VV", NULL, BLIST_REPORTLUN2},
	{"ADAPTEC", "AACRAID", NULL, BLIST_FORCELUN},
	{"ADAPTEC", "Adaptec 5400S", NULL, BLIST_FORCELUN},
	{"AFT PRO", "-IX CF", "0.0>", BLIST_FORCELUN},
	{"BELKIN", "USB 2 HS-CF", "1.95",  BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"BROWNIE", "1200U3P", NULL, BLIST_NOREPORTLUN},
	{"BROWNIE", "1600U3P", NULL, BLIST_NOREPORTLUN},
	{"CANON", "IPUBJD", NULL, BLIST_SPARSELUN},
	{"CBOX3", "USB Storage-SMC", "300A", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"CMD", "CRA-7280", NULL, BLIST_SPARSELUN},	/* CMD RAID Controller */
	{"CNSI", "G7324", NULL, BLIST_SPARSELUN},	/* Chaparral G7324 RAID */
	{"CNSi", "G8324", NULL, BLIST_SPARSELUN},	/* Chaparral G8324 RAID */
	{"COMPAQ", "ARRAY CONTROLLER", NULL, BLIST_SPARSELUN | BLIST_LARGELUN |
		BLIST_MAX_512 | BLIST_REPORTLUN2},	/* Compaq RA4x00 */
	{"COMPAQ", "LOGICAL VOLUME", NULL, BLIST_FORCELUN | BLIST_MAX_512}, /* Compaq RA4x00 */
	{"COMPAQ", "CR3500", NULL, BLIST_FORCELUN},
	{"COMPAQ", "MSA1000", NULL, BLIST_SPARSELUN | BLIST_NOSTARTONADD},
	{"COMPAQ", "MSA1000 VOLUME", NULL, BLIST_SPARSELUN | BLIST_NOSTARTONADD},
	{"COMPAQ", "HSV110", NULL, BLIST_REPORTLUN2 | BLIST_NOSTARTONADD},
	{"DDN", "SAN DataDirector", "*", BLIST_SPARSELUN},
	{"DEC", "HSG80", NULL, BLIST_REPORTLUN2 | BLIST_NOSTARTONADD},
	{"DELL", "PV660F", NULL, BLIST_SPARSELUN},
	{"DELL", "PV660F   PSEUDO", NULL, BLIST_SPARSELUN},
	{"DELL", "PSEUDO DEVICE .", NULL, BLIST_SPARSELUN},	/* Dell PV 530F */
	{"DELL", "PV530F", NULL, BLIST_SPARSELUN},
	{"DELL", "PERCRAID", NULL, BLIST_FORCELUN},
	{"DGC", "RAID", NULL, BLIST_SPARSELUN},	/* Dell PV 650F, storage on LUN 0 */
	{"DGC", "DISK", NULL, BLIST_SPARSELUN},	/* Dell PV 650F, no storage on LUN 0 */
	{"EMC",  "Invista", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"EMC", "SYMMETRIX", NULL, BLIST_SPARSELUN | BLIST_LARGELUN | BLIST_FORCELUN},
	{"EMULEX", "MD21/S2     ESDI", NULL, BLIST_SINGLELUN},
	{"easyRAID", "16P", NULL, BLIST_NOREPORTLUN},
	{"easyRAID", "X6P", NULL, BLIST_NOREPORTLUN},
	{"easyRAID", "F8", NULL, BLIST_NOREPORTLUN},
	{"FSC", "CentricStor", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"Generic", "USB SD Reader", "1.00", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"Generic", "USB Storage-SMC", "0180", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"Generic", "USB Storage-SMC", "0207", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"HITACHI", "DF400", "*", BLIST_REPORTLUN2},
	{"HITACHI", "DF500", "*", BLIST_REPORTLUN2},
	{"HITACHI", "DISK-SUBSYSTEM", "*", BLIST_REPORTLUN2},
	{"HITACHI", "HUS1530", "*", BLIST_NO_DIF},
	{"HITACHI", "OPEN-", "*", BLIST_REPORTLUN2},
	{"HITACHI", "OP-C-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HITACHI", "3380-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HITACHI", "3390-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HITACHI", "6586-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HITACHI", "6588-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "A6189A", NULL, BLIST_SPARSELUN | BLIST_LARGELUN},	/* HP VA7400 */
	{"HP", "OPEN-", "*", BLIST_REPORTLUN2}, /* HP XP Arrays */
	{"HP", "NetRAID-4M", NULL, BLIST_FORCELUN},
	{"HP", "HSV100", NULL, BLIST_REPORTLUN2 | BLIST_NOSTARTONADD},
	{"HP", "C1557A", NULL, BLIST_FORCELUN},
	{"HP", "C3323-300", "4269", BLIST_NOTQ},
	{"HP", "C5713A", NULL, BLIST_NOREPORTLUN},
	{"HP", "DF400", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "DF500", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "DF600", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "OP-C-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "3380-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "3390-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "6586-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"HP", "6588-", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"IBM", "AuSaV1S2", NULL, BLIST_FORCELUN},
	{"IBM", "ProFibre 4000R", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"IBM", "2105", NULL, BLIST_RETRY_HWERROR},
	{"iomega", "jaz 1GB", "J.86", BLIST_NOTQ | BLIST_NOLUN},
	{"IOMEGA", "ZIP", NULL, BLIST_NOTQ | BLIST_NOLUN},
	{"IOMEGA", "Io20S         *F", NULL, BLIST_KEY},
	{"INSITE", "Floptical   F*8I", NULL, BLIST_KEY},
	{"INSITE", "I325VM", NULL, BLIST_KEY},
	{"Intel", "Multi-Flex", NULL, BLIST_NO_RSOC},
	{"iRiver", "iFP Mass Driver", NULL, BLIST_NOT_LOCKABLE | BLIST_INQUIRY_36},
	{"LASOUND", "CDX7405", "3.10", BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"Marvell", "Console", NULL, BLIST_SKIP_VPD_PAGES},
	{"Marvell", "91xx Config", "1.01", BLIST_SKIP_VPD_PAGES},
	{"MATSHITA", "PD-1", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"MATSHITA", "DMC-LC5", NULL, BLIST_NOT_LOCKABLE | BLIST_INQUIRY_36},
	{"MATSHITA", "DMC-LC40", NULL, BLIST_NOT_LOCKABLE | BLIST_INQUIRY_36},
	{"Medion", "Flash XL  MMC/SD", "2.6D", BLIST_FORCELUN},
	{"MegaRAID", "LD", NULL, BLIST_FORCELUN},
	{"MICROP", "4110", NULL, BLIST_NOTQ},
	{"MSFT", "Virtual HD", NULL, BLIST_NO_RSOC},
	{"MYLEX", "DACARMRB", "*", BLIST_REPORTLUN2},
	{"nCipher", "Fastness Crypto", NULL, BLIST_FORCELUN},
	{"NAKAMICH", "MJ-4.8S", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-5.16S", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NEC", "PD-1 ODX654P", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NEC", "iStorage", NULL, BLIST_REPORTLUN2},
	{"NRC", "MBR-7", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NRC", "MBR-7.4", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-600", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-602X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-604X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-624X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"Promise", "VTrak E610f", NULL, BLIST_SPARSELUN | BLIST_NO_RSOC},
	{"Promise", "", NULL, BLIST_SPARSELUN},
	{"QEMU", "QEMU CD-ROM", NULL, BLIST_SKIP_VPD_PAGES},
	{"QNAP", "iSCSI Storage", NULL, BLIST_MAX_1024},
	{"SYNOLOGY", "iSCSI Storage", NULL, BLIST_MAX_1024},
	{"QUANTUM", "XP34301", "1071", BLIST_NOTQ},
	{"REGAL", "CDC-4X", NULL, BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"SanDisk", "ImageMate CF-SD1", NULL, BLIST_FORCELUN},
	{"SEAGATE", "ST34555N", "0930", BLIST_NOTQ},	/* Chokes on tagged INQUIRY */
	{"SEAGATE", "ST3390N", "9546", BLIST_NOTQ},
	{"SEAGATE", "ST900MM0006", NULL, BLIST_SKIP_VPD_PAGES},
	{"SGI", "RAID3", "*", BLIST_SPARSELUN},
	{"SGI", "RAID5", "*", BLIST_SPARSELUN},
	{"SGI", "TP9100", "*", BLIST_REPORTLUN2},
	{"SGI", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"IBM", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"SUN", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"DELL", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"STK", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"NETAPP", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"LSI", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"ENGENIO", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"SMSC", "USB 2 HS-CF", NULL, BLIST_SPARSELUN | BLIST_INQUIRY_36},
	{"SONY", "CD-ROM CDU-8001", NULL, BLIST_BORKEN},
	{"SONY", "TSL", NULL, BLIST_FORCELUN},		/* DDS3 & DDS4 autoloaders */
	{"ST650211", "CF", NULL, BLIST_RETRY_HWERROR},
	{"SUN", "T300", "*", BLIST_SPARSELUN},
	{"SUN", "T4", "*", BLIST_SPARSELUN},
	{"TEXEL", "CD-ROM", "1.06", BLIST_BORKEN},
	{"Tornado-", "F4", "*", BLIST_NOREPORTLUN},
	{"TOSHIBA", "CDROM", NULL, BLIST_ISROM},
	{"TOSHIBA", "CD-ROM", NULL, BLIST_ISROM},
	{"Traxdata", "CDR4120", NULL, BLIST_NOLUN},	/* locks up */
	{"USB2.0", "SMARTMEDIA/XD", NULL, BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"WangDAT", "Model 2600", "01.7", BLIST_SELECT_NO_ATN},
	{"WangDAT", "Model 3200", "02.2", BLIST_SELECT_NO_ATN},
	{"WangDAT", "Model 1300", "02.4", BLIST_SELECT_NO_ATN},
	{"WDC WD25", "00JB-00FUA0", NULL, BLIST_NOREPORTLUN},
	{"XYRATEX", "RS", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"Zzyzx", "RocketStor 500S", NULL, BLIST_SPARSELUN},
	{"Zzyzx", "RocketStor 2000", NULL, BLIST_SPARSELUN},
	{ NULL, NULL, NULL, 0 },
};

static struct scsi_dev_info_list_table *scsi_devinfo_lookup_by_key(int key)
{
	struct scsi_dev_info_list_table *devinfo_table;
	int found = 0;

	list_for_each_entry(devinfo_table, &scsi_dev_info_list, node)
		if (devinfo_table->key == key) {
			found = 1;
			break;
		}
	if (!found)
		return ERR_PTR(-EINVAL);

	return devinfo_table;
}

/*
 * scsi_strcpy_devinfo: called from scsi_dev_info_list_add to copy into
 * devinfo vendor and model strings.
 */
static void scsi_strcpy_devinfo(char *name, char *to, size_t to_length,
				char *from, int compatible)
{
	size_t from_length;

	from_length = strlen(from);
	strncpy(to, from, min(to_length, from_length));
	if (from_length < to_length) {
		if (compatible) {
			/*
			 * NUL terminate the string if it is short.
			 */
			to[from_length] = '\0';
		} else {
			/* 
			 * space pad the string if it is short. 
			 */
			strncpy(&to[from_length], spaces,
				to_length - from_length);
		}
	}
	if (from_length > to_length)
		 printk(KERN_WARNING "%s: %s string '%s' is too long\n",
			__func__, name, from);
}

/**
 * scsi_dev_info_list_add - add one dev_info list entry.
 * @compatible: if true, null terminate short strings.  Otherwise space pad.
 * @vendor:	vendor string
 * @model:	model (product) string
 * @strflags:	integer string
 * @flags:	if strflags NULL, use this flag value
 *
 * Description:
 * 	Create and add one dev_info entry for @vendor, @model, @strflags or
 * 	@flag. If @compatible, add to the tail of the list, do not space
 * 	pad, and set devinfo->compatible. The scsi_static_device_list entries
 * 	are added with @compatible 1 and @clfags NULL.
 *
 * Returns: 0 OK, -error on failure.
 **/
static int scsi_dev_info_list_add(int compatible, char *vendor, char *model,
			    char *strflags, int flags)
{
	return scsi_dev_info_list_add_keyed(compatible, vendor, model,
					    strflags, flags,
					    SCSI_DEVINFO_GLOBAL);
}

/**
 * scsi_dev_info_list_add_keyed - add one dev_info list entry.
 * @compatible: if true, null terminate short strings.  Otherwise space pad.
 * @vendor:	vendor string
 * @model:	model (product) string
 * @strflags:	integer string
 * @flags:	if strflags NULL, use this flag value
 * @key:	specify list to use
 *
 * Description:
 * 	Create and add one dev_info entry for @vendor, @model,
 * 	@strflags or @flag in list specified by @key. If @compatible,
 * 	add to the tail of the list, do not space pad, and set
 * 	devinfo->compatible. The scsi_static_device_list entries are
 * 	added with @compatible 1 and @clfags NULL.
 *
 * Returns: 0 OK, -error on failure.
 **/
int scsi_dev_info_list_add_keyed(int compatible, char *vendor, char *model,
				 char *strflags, int flags, int key)
{
	struct scsi_dev_info_list *devinfo;
	struct scsi_dev_info_list_table *devinfo_table =
		scsi_devinfo_lookup_by_key(key);

	if (IS_ERR(devinfo_table))
		return PTR_ERR(devinfo_table);

	devinfo = kmalloc(sizeof(*devinfo), GFP_KERNEL);
	if (!devinfo) {
		printk(KERN_ERR "%s: no memory\n", __func__);
		return -ENOMEM;
	}

	scsi_strcpy_devinfo("vendor", devinfo->vendor, sizeof(devinfo->vendor),
			    vendor, compatible);
	scsi_strcpy_devinfo("model", devinfo->model, sizeof(devinfo->model),
			    model, compatible);

	if (strflags)
		devinfo->flags = simple_strtoul(strflags, NULL, 0);
	else
		devinfo->flags = flags;

	devinfo->compatible = compatible;

	if (compatible)
		list_add_tail(&devinfo->dev_info_list,
			      &devinfo_table->scsi_dev_info_list);
	else
		list_add(&devinfo->dev_info_list,
			 &devinfo_table->scsi_dev_info_list);

	return 0;
}
EXPORT_SYMBOL(scsi_dev_info_list_add_keyed);

/**
 * scsi_dev_info_list_find - find a matching dev_info list entry.
 * @vendor:	vendor string
 * @model:	model (product) string
 * @key:	specify list to use
 *
 * Description:
 *	Finds the first dev_info entry matching @vendor, @model
 * 	in list specified by @key.
 *
 * Returns: pointer to matching entry, or ERR_PTR on failure.
 **/
static struct scsi_dev_info_list *scsi_dev_info_list_find(const char *vendor,
		const char *model, int key)
{
	struct scsi_dev_info_list *devinfo;
	struct scsi_dev_info_list_table *devinfo_table =
		scsi_devinfo_lookup_by_key(key);
	size_t vmax, mmax;
	const char *vskip, *mskip;

	if (IS_ERR(devinfo_table))
		return (struct scsi_dev_info_list *) devinfo_table;

	/* Prepare for "compatible" matches */

	/*
	 * XXX why skip leading spaces? If an odd INQUIRY
	 * value, that should have been part of the
	 * scsi_static_device_list[] entry, such as "  FOO"
	 * rather than "FOO". Since this code is already
	 * here, and we don't know what device it is
	 * trying to work with, leave it as-is.
	 */
	vmax = sizeof(devinfo->vendor);
	vskip = vendor;
	while (vmax > 0 && *vskip == ' ') {
		vmax--;
		vskip++;
	}
	/* Also skip trailing spaces */
	while (vmax > 0 && vskip[vmax - 1] == ' ')
		--vmax;

	mmax = sizeof(devinfo->model);
	mskip = model;
	while (mmax > 0 && *mskip == ' ') {
		mmax--;
		mskip++;
	}
	while (mmax > 0 && mskip[mmax - 1] == ' ')
		--mmax;

	list_for_each_entry(devinfo, &devinfo_table->scsi_dev_info_list,
			    dev_info_list) {
		if (devinfo->compatible) {
			/*
			 * Behave like the older version of get_device_flags.
			 */
			if (memcmp(devinfo->vendor, vskip, vmax) ||
					(vmax < sizeof(devinfo->vendor) &&
						devinfo->vendor[vmax]))
				continue;
			if (memcmp(devinfo->model, mskip, mmax) ||
					(mmax < sizeof(devinfo->model) &&
						devinfo->model[mmax]))
				continue;
			return devinfo;
		} else {
			if (!memcmp(devinfo->vendor, vendor,
				     sizeof(devinfo->vendor)) &&
			     !memcmp(devinfo->model, model,
				      sizeof(devinfo->model)))
				return devinfo;
		}
	}

	return ERR_PTR(-ENOENT);
}

/**
 * scsi_dev_info_list_del_keyed - remove one dev_info list entry.
 * @vendor:	vendor string
 * @model:	model (product) string
 * @key:	specify list to use
 *
 * Description:
 *	Remove and destroy one dev_info entry for @vendor, @model
 *	in list specified by @key.
 *
 * Returns: 0 OK, -error on failure.
 **/
int scsi_dev_info_list_del_keyed(char *vendor, char *model, int key)
{
	struct scsi_dev_info_list *found;

	found = scsi_dev_info_list_find(vendor, model, key);
	if (IS_ERR(found))
		return PTR_ERR(found);

	list_del(&found->dev_info_list);
	kfree(found);
	return 0;
}
EXPORT_SYMBOL(scsi_dev_info_list_del_keyed);

/**
 * scsi_dev_info_list_add_str - parse dev_list and add to the scsi_dev_info_list.
 * @dev_list:	string of device flags to add
 *
 * Description:
 * 	Parse dev_list, and add entries to the scsi_dev_info_list.
 * 	dev_list is of the form "vendor:product:flag,vendor:product:flag".
 * 	dev_list is modified via strsep. Can be called for command line
 * 	addition, for proc or mabye a sysfs interface.
 *
 * Returns: 0 if OK, -error on failure.
 **/
static int scsi_dev_info_list_add_str(char *dev_list)
{
	char *vendor, *model, *strflags, *next;
	char *next_check;
	int res = 0;

	next = dev_list;
	if (next && next[0] == '"') {
		/*
		 * Ignore both the leading and trailing quote.
		 */
		next++;
		next_check = ",\"";
	} else {
		next_check = ",";
	}

	/*
	 * For the leading and trailing '"' case, the for loop comes
	 * through the last time with vendor[0] == '\0'.
	 */
	for (vendor = strsep(&next, ":"); vendor && (vendor[0] != '\0')
	     && (res == 0); vendor = strsep(&next, ":")) {
		strflags = NULL;
		model = strsep(&next, ":");
		if (model)
			strflags = strsep(&next, next_check);
		if (!model || !strflags) {
			printk(KERN_ERR "%s: bad dev info string '%s' '%s'"
			       " '%s'\n", __func__, vendor, model,
			       strflags);
			res = -EINVAL;
		} else
			res = scsi_dev_info_list_add(0 /* compatible */, vendor,
						     model, strflags, 0);
	}
	return res;
}

/**
 * get_device_flags - get device specific flags from the dynamic device list.
 * @sdev:       &scsi_device to get flags for
 * @vendor:	vendor name
 * @model:	model name
 *
 * Description:
 *     Search the global scsi_dev_info_list (specified by list zero)
 *     for an entry matching @vendor and @model, if found, return the
 *     matching flags value, else return the host or global default
 *     settings.  Called during scan time.
 **/
int scsi_get_device_flags(struct scsi_device *sdev,
			  const unsigned char *vendor,
			  const unsigned char *model)
{
	return scsi_get_device_flags_keyed(sdev, vendor, model,
					   SCSI_DEVINFO_GLOBAL);
}


/**
 * scsi_get_device_flags_keyed - get device specific flags from the dynamic device list
 * @sdev:       &scsi_device to get flags for
 * @vendor:	vendor name
 * @model:	model name
 * @key:	list to look up
 *
 * Description:
 *     Search the scsi_dev_info_list specified by @key for an entry
 *     matching @vendor and @model, if found, return the matching
 *     flags value, else return the host or global default settings.
 *     Called during scan time.
 **/
int scsi_get_device_flags_keyed(struct scsi_device *sdev,
				const unsigned char *vendor,
				const unsigned char *model,
				int key)
{
	struct scsi_dev_info_list *devinfo;
	int err;

	devinfo = scsi_dev_info_list_find(vendor, model, key);
	if (!IS_ERR(devinfo))
		return devinfo->flags;

	err = PTR_ERR(devinfo);
	if (err != -ENOENT)
		return err;

	/* nothing found, return nothing */
	if (key != SCSI_DEVINFO_GLOBAL)
		return 0;

	/* except for the global list, where we have an exception */
	if (sdev->sdev_bflags)
		return sdev->sdev_bflags;

	return scsi_default_dev_flags;
}
EXPORT_SYMBOL(scsi_get_device_flags_keyed);

#ifdef CONFIG_SCSI_PROC_FS
struct double_list {
	struct list_head *top;
	struct list_head *bottom;
};

static int devinfo_seq_show(struct seq_file *m, void *v)
{
	struct double_list *dl = v;
	struct scsi_dev_info_list_table *devinfo_table =
		list_entry(dl->top, struct scsi_dev_info_list_table, node);
	struct scsi_dev_info_list *devinfo =
		list_entry(dl->bottom, struct scsi_dev_info_list,
			   dev_info_list);

	if (devinfo_table->scsi_dev_info_list.next == dl->bottom &&
	    devinfo_table->name)
		seq_printf(m, "[%s]:\n", devinfo_table->name);

	seq_printf(m, "'%.8s' '%.16s' 0x%x\n",
		   devinfo->vendor, devinfo->model, devinfo->flags);
	return 0;
}

static void *devinfo_seq_start(struct seq_file *m, loff_t *ppos)
{
	struct double_list *dl = kmalloc(sizeof(*dl), GFP_KERNEL);
	loff_t pos = *ppos;

	if (!dl)
		return NULL;

	list_for_each(dl->top, &scsi_dev_info_list) {
		struct scsi_dev_info_list_table *devinfo_table =
			list_entry(dl->top, struct scsi_dev_info_list_table,
				   node);
		list_for_each(dl->bottom, &devinfo_table->scsi_dev_info_list)
			if (pos-- == 0)
				return dl;
	}

	kfree(dl);
	return NULL;
}

static void *devinfo_seq_next(struct seq_file *m, void *v, loff_t *ppos)
{
	struct double_list *dl = v;
	struct scsi_dev_info_list_table *devinfo_table =
		list_entry(dl->top, struct scsi_dev_info_list_table, node);

	++*ppos;
	dl->bottom = dl->bottom->next;
	while (&devinfo_table->scsi_dev_info_list == dl->bottom) {
		dl->top = dl->top->next;
		if (dl->top == &scsi_dev_info_list) {
			kfree(dl);
			return NULL;
		}
		devinfo_table = list_entry(dl->top,
					   struct scsi_dev_info_list_table,
					   node);
		dl->bottom = devinfo_table->scsi_dev_info_list.next;
	}

	return dl;
}

static void devinfo_seq_stop(struct seq_file *m, void *v)
{
	kfree(v);
}

static const struct seq_operations scsi_devinfo_seq_ops = {
	.start	= devinfo_seq_start,
	.next	= devinfo_seq_next,
	.stop	= devinfo_seq_stop,
	.show	= devinfo_seq_show,
};

static int proc_scsi_devinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scsi_devinfo_seq_ops);
}

/* 
 * proc_scsi_dev_info_write - allow additions to scsi_dev_info_list via /proc.
 *
 * Description: Adds a black/white list entry for vendor and model with an
 * integer value of flag to the scsi device info list.
 * To use, echo "vendor:model:flag" > /proc/scsi/device_info
 */
static ssize_t proc_scsi_devinfo_write(struct file *file,
				       const char __user *buf,
				       size_t length, loff_t *ppos)
{
	char *buffer;
	ssize_t err = length;

	if (!buf || length>PAGE_SIZE)
		return -EINVAL;
	if (!(buffer = (char *) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	if (copy_from_user(buffer, buf, length)) {
		err =-EFAULT;
		goto out;
	}

	if (length < PAGE_SIZE)
		buffer[length] = '\0';
	else if (buffer[PAGE_SIZE-1]) {
		err = -EINVAL;
		goto out;
	}

	scsi_dev_info_list_add_str(buffer);

out:
	free_page((unsigned long)buffer);
	return err;
}

static const struct file_operations scsi_devinfo_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_scsi_devinfo_open,
	.read		= seq_read,
	.write		= proc_scsi_devinfo_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
#endif /* CONFIG_SCSI_PROC_FS */

module_param_string(dev_flags, scsi_dev_flags, sizeof(scsi_dev_flags), 0);
MODULE_PARM_DESC(dev_flags,
	 "Given scsi_dev_flags=vendor:model:flags[,v:m:f] add black/white"
	 " list entries for vendor and model with an integer value of flags"
	 " to the scsi device info list");

module_param_named(default_dev_flags, scsi_default_dev_flags, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(default_dev_flags,
		 "scsi default device flag integer value");

/**
 * scsi_exit_devinfo - remove /proc/scsi/device_info & the scsi_dev_info_list
 **/
void scsi_exit_devinfo(void)
{
#ifdef CONFIG_SCSI_PROC_FS
	remove_proc_entry("scsi/device_info", NULL);
#endif

	scsi_dev_info_remove_list(SCSI_DEVINFO_GLOBAL);
}

/**
 * scsi_dev_info_add_list - add a new devinfo list
 * @key:	key of the list to add
 * @name:	Name of the list to add (for /proc/scsi/device_info)
 *
 * Adds the requested list, returns zero on success, -EEXIST if the
 * key is already registered to a list, or other error on failure.
 */
int scsi_dev_info_add_list(int key, const char *name)
{
	struct scsi_dev_info_list_table *devinfo_table =
		scsi_devinfo_lookup_by_key(key);

	if (!IS_ERR(devinfo_table))
		/* list already exists */
		return -EEXIST;

	devinfo_table = kmalloc(sizeof(*devinfo_table), GFP_KERNEL);

	if (!devinfo_table)
		return -ENOMEM;

	INIT_LIST_HEAD(&devinfo_table->node);
	INIT_LIST_HEAD(&devinfo_table->scsi_dev_info_list);
	devinfo_table->name = name;
	devinfo_table->key = key;
	list_add_tail(&devinfo_table->node, &scsi_dev_info_list);

	return 0;
}
EXPORT_SYMBOL(scsi_dev_info_add_list);

/**
 * scsi_dev_info_remove_list - destroy an added devinfo list
 * @key: key of the list to destroy
 *
 * Iterates over the entire list first, freeing all the values, then
 * frees the list itself.  Returns 0 on success or -EINVAL if the key
 * can't be found.
 */
int scsi_dev_info_remove_list(int key)
{
	struct list_head *lh, *lh_next;
	struct scsi_dev_info_list_table *devinfo_table =
		scsi_devinfo_lookup_by_key(key);

	if (IS_ERR(devinfo_table))
		/* no such list */
		return -EINVAL;

	/* remove from the master list */
	list_del(&devinfo_table->node);

	list_for_each_safe(lh, lh_next, &devinfo_table->scsi_dev_info_list) {
		struct scsi_dev_info_list *devinfo;

		devinfo = list_entry(lh, struct scsi_dev_info_list,
				     dev_info_list);
		kfree(devinfo);
	}
	kfree(devinfo_table);

	return 0;
}
EXPORT_SYMBOL(scsi_dev_info_remove_list);

/**
 * scsi_init_devinfo - set up the dynamic device list.
 *
 * Description:
 * 	Add command line entries from scsi_dev_flags, then add
 * 	scsi_static_device_list entries to the scsi device info list.
 */
int __init scsi_init_devinfo(void)
{
#ifdef CONFIG_SCSI_PROC_FS
	struct proc_dir_entry *p;
#endif
	int error, i;

	error = scsi_dev_info_add_list(SCSI_DEVINFO_GLOBAL, NULL);
	if (error)
		return error;

	error = scsi_dev_info_list_add_str(scsi_dev_flags);
	if (error)
		goto out;

	for (i = 0; scsi_static_device_list[i].vendor; i++) {
		error = scsi_dev_info_list_add(1 /* compatibile */,
				scsi_static_device_list[i].vendor,
				scsi_static_device_list[i].model,
				NULL,
				scsi_static_device_list[i].flags);
		if (error)
			goto out;
	}

#ifdef CONFIG_SCSI_PROC_FS
	p = proc_create("scsi/device_info", 0, NULL, &scsi_devinfo_proc_fops);
	if (!p) {
		error = -ENOMEM;
		goto out;
	}
#endif /* CONFIG_SCSI_PROC_FS */

 out:
	if (error)
		scsi_exit_devinfo();
	return error;
}
