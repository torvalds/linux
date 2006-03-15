
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

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
	{"BROWNIE", "1600U3P", NULL, BLIST_NOREPORTLUN},
	{"CANON", "IPUBJD", NULL, BLIST_SPARSELUN},
	{"CBOX3", "USB Storage-SMC", "300A", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"CMD", "CRA-7280", NULL, BLIST_SPARSELUN},	/* CMD RAID Controller */
	{"CNSI", "G7324", NULL, BLIST_SPARSELUN},	/* Chaparral G7324 RAID */
	{"CNSi", "G8324", NULL, BLIST_SPARSELUN},	/* Chaparral G8324 RAID */
	{"COMPAQ", "LOGICAL VOLUME", NULL, BLIST_FORCELUN},
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
	{"EMC", "SYMMETRIX", NULL, BLIST_SPARSELUN | BLIST_LARGELUN | BLIST_FORCELUN},
	{"EMULEX", "MD21/S2     ESDI", NULL, BLIST_SINGLELUN},
	{"FSC", "CentricStor", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"Generic", "USB SD Reader", "1.00", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"Generic", "USB Storage-SMC", "0180", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"Generic", "USB Storage-SMC", "0207", BLIST_FORCELUN | BLIST_INQUIRY_36},
	{"HITACHI", "DF400", "*", BLIST_SPARSELUN},
	{"HITACHI", "DF500", "*", BLIST_SPARSELUN},
	{"HITACHI", "DF600", "*", BLIST_SPARSELUN},
	{"HP", "A6189A", NULL, BLIST_SPARSELUN | BLIST_LARGELUN},	/* HP VA7400 */
	{"HP", "OPEN-", "*", BLIST_SPARSELUN | BLIST_LARGELUN}, /* HP XP Arrays */
	{"HP", "NetRAID-4M", NULL, BLIST_FORCELUN},
	{"HP", "HSV100", NULL, BLIST_REPORTLUN2 | BLIST_NOSTARTONADD},
	{"HP", "C1557A", NULL, BLIST_FORCELUN},
	{"HP", "C3323-300", "4269", BLIST_NOTQ},
	{"IBM", "AuSaV1S2", NULL, BLIST_FORCELUN},
	{"IBM", "ProFibre 4000R", "*", BLIST_SPARSELUN | BLIST_LARGELUN},
	{"IBM", "2105", NULL, BLIST_RETRY_HWERROR},
	{"iomega", "jaz 1GB", "J.86", BLIST_NOTQ | BLIST_NOLUN},
	{"IOMEGA", "Io20S         *F", NULL, BLIST_KEY},
	{"INSITE", "Floptical   F*8I", NULL, BLIST_KEY},
	{"INSITE", "I325VM", NULL, BLIST_KEY},
	{"iRiver", "iFP Mass Driver", NULL, BLIST_NOT_LOCKABLE | BLIST_INQUIRY_36},
	{"LASOUND", "CDX7405", "3.10", BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"MATSHITA", "PD-1", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"MATSHITA", "DMC-LC5", NULL, BLIST_NOT_LOCKABLE | BLIST_INQUIRY_36},
	{"MATSHITA", "DMC-LC40", NULL, BLIST_NOT_LOCKABLE | BLIST_INQUIRY_36},
	{"Medion", "Flash XL  MMC/SD", "2.6D", BLIST_FORCELUN},
	{"MegaRAID", "LD", NULL, BLIST_FORCELUN},
	{"MICROP", "4110", NULL, BLIST_NOTQ},
	{"MYLEX", "DACARMRB", "*", BLIST_REPORTLUN2},
	{"nCipher", "Fastness Crypto", NULL, BLIST_FORCELUN},
	{"NAKAMICH", "MJ-4.8S", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-5.16S", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NEC", "PD-1 ODX654P", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NRC", "MBR-7", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NRC", "MBR-7.4", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-600", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-602X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-604X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"PIONEER", "CD-ROM DRM-624X", NULL, BLIST_FORCELUN | BLIST_SINGLELUN},
	{"REGAL", "CDC-4X", NULL, BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"SanDisk", "ImageMate CF-SD1", NULL, BLIST_FORCELUN},
	{"SEAGATE", "ST34555N", "0930", BLIST_NOTQ},	/* Chokes on tagged INQUIRY */
	{"SEAGATE", "ST3390N", "9546", BLIST_NOTQ},
	{"SGI", "RAID3", "*", BLIST_SPARSELUN},
	{"SGI", "RAID5", "*", BLIST_SPARSELUN},
	{"SGI", "TP9100", "*", BLIST_REPORTLUN2},
	{"SGI", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"IBM", "Universal Xport", "*", BLIST_NO_ULD_ATTACH},
	{"SMSC", "USB 2 HS-CF", NULL, BLIST_SPARSELUN | BLIST_INQUIRY_36},
	{"SONY", "CD-ROM CDU-8001", NULL, BLIST_BORKEN},
	{"SONY", "TSL", NULL, BLIST_FORCELUN},		/* DDS3 & DDS4 autoloaders */
	{"ST650211", "CF", NULL, BLIST_RETRY_HWERROR},
	{"SUN", "T300", "*", BLIST_SPARSELUN},
	{"SUN", "T4", "*", BLIST_SPARSELUN},
	{"TEXEL", "CD-ROM", "1.06", BLIST_BORKEN},
	{"TOSHIBA", "CDROM", NULL, BLIST_ISROM},
	{"TOSHIBA", "CD-ROM", NULL, BLIST_ISROM},
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
			__FUNCTION__, name, from);
}

/**
 * scsi_dev_info_list_add: add one dev_info list entry.
 * @vendor:	vendor string
 * @model:	model (product) string
 * @strflags:	integer string
 * @flag:	if strflags NULL, use this flag value
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
	struct scsi_dev_info_list *devinfo;

	devinfo = kmalloc(sizeof(*devinfo), GFP_KERNEL);
	if (!devinfo) {
		printk(KERN_ERR "%s: no memory\n", __FUNCTION__);
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
		list_add_tail(&devinfo->dev_info_list, &scsi_dev_info_list);
	else
		list_add(&devinfo->dev_info_list, &scsi_dev_info_list);

	return 0;
}

/**
 * scsi_dev_info_list_add_str: parse dev_list and add to the
 * scsi_dev_info_list.
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
			       " '%s'\n", __FUNCTION__, vendor, model,
			       strflags);
			res = -EINVAL;
		} else
			res = scsi_dev_info_list_add(0 /* compatible */, vendor,
						     model, strflags, 0);
	}
	return res;
}

/**
 * get_device_flags - get device specific flags from the dynamic device
 * list. Called during scan time.
 * @vendor:	vendor name
 * @model:	model name
 *
 * Description:
 *     Search the scsi_dev_info_list for an entry matching @vendor and
 *     @model, if found, return the matching flags value, else return
 *     the host or global default settings.
 **/
int scsi_get_device_flags(struct scsi_device *sdev,
			  const unsigned char *vendor,
			  const unsigned char *model)
{
	struct scsi_dev_info_list *devinfo;
	unsigned int bflags;

	bflags = sdev->sdev_bflags;
	if (!bflags)
		bflags = scsi_default_dev_flags;

	list_for_each_entry(devinfo, &scsi_dev_info_list, dev_info_list) {
		if (devinfo->compatible) {
			/*
			 * Behave like the older version of get_device_flags.
			 */
			size_t max;
			/*
			 * XXX why skip leading spaces? If an odd INQUIRY
			 * value, that should have been part of the
			 * scsi_static_device_list[] entry, such as "  FOO"
			 * rather than "FOO". Since this code is already
			 * here, and we don't know what device it is
			 * trying to work with, leave it as-is.
			 */
			max = 8;	/* max length of vendor */
			while ((max > 0) && *vendor == ' ') {
				max--;
				vendor++;
			}
			/*
			 * XXX removing the following strlen() would be
			 * good, using it means that for a an entry not in
			 * the list, we scan every byte of every vendor
			 * listed in scsi_static_device_list[], and never match
			 * a single one (and still have to compare at
			 * least the first byte of each vendor).
			 */
			if (memcmp(devinfo->vendor, vendor,
				    min(max, strlen(devinfo->vendor))))
				continue;
			/*
			 * Skip spaces again.
			 */
			max = 16;	/* max length of model */
			while ((max > 0) && *model == ' ') {
				max--;
				model++;
			}
			if (memcmp(devinfo->model, model,
				   min(max, strlen(devinfo->model))))
				continue;
			return devinfo->flags;
		} else {
			if (!memcmp(devinfo->vendor, vendor,
				     sizeof(devinfo->vendor)) &&
			     !memcmp(devinfo->model, model,
				      sizeof(devinfo->model)))
				return devinfo->flags;
		}
	}
	return bflags;
}

#ifdef CONFIG_SCSI_PROC_FS
/* 
 * proc_scsi_dev_info_read: dump the scsi_dev_info_list via
 * /proc/scsi/device_info
 */
static int proc_scsi_devinfo_read(char *buffer, char **start,
				  off_t offset, int length)
{
	struct scsi_dev_info_list *devinfo;
	int size, len = 0;
	off_t begin = 0;
	off_t pos = 0;

	list_for_each_entry(devinfo, &scsi_dev_info_list, dev_info_list) {
		size = sprintf(buffer + len, "'%.8s' '%.16s' 0x%x\n",
			devinfo->vendor, devinfo->model, devinfo->flags);
		len += size;
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
		if (pos > offset + length)
			goto stop_output;
	}

stop_output:
	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);	/* Start slop */
	if (len > length)
		len = length;	/* Ending slop */
	return (len);
}

/* 
 * proc_scsi_dev_info_write: allow additions to the scsi_dev_info_list via
 * /proc.
 *
 * Use: echo "vendor:model:flag" > /proc/scsi/device_info
 *
 * To add a black/white list entry for vendor and model with an integer
 * value of flag to the scsi device info list.
 */
static int proc_scsi_devinfo_write(struct file *file, const char __user *buf,
				   unsigned long length, void *data)
{
	char *buffer;
	int err = length;

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
 * scsi_dev_info_list_delete: called from scsi.c:exit_scsi to remove
 * 	the scsi_dev_info_list.
 **/
void scsi_exit_devinfo(void)
{
	struct list_head *lh, *lh_next;
	struct scsi_dev_info_list *devinfo;

#ifdef CONFIG_SCSI_PROC_FS
	remove_proc_entry("scsi/device_info", NULL);
#endif

	list_for_each_safe(lh, lh_next, &scsi_dev_info_list) {
		devinfo = list_entry(lh, struct scsi_dev_info_list,
				     dev_info_list);
		kfree(devinfo);
	}
}

/**
 * scsi_dev_list_init: set up the dynamic device list.
 * @dev_list:	string of device flags to add
 *
 * Description:
 * 	Add command line @dev_list entries, then add
 * 	scsi_static_device_list entries to the scsi device info list.
 **/
int __init scsi_init_devinfo(void)
{
#ifdef CONFIG_SCSI_PROC_FS
	struct proc_dir_entry *p;
#endif
	int error, i;

	error = scsi_dev_info_list_add_str(scsi_dev_flags);
	if (error)
		return error;

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
	p = create_proc_entry("scsi/device_info", 0, NULL);
	if (!p) {
		error = -ENOMEM;
		goto out;
	}

	p->owner = THIS_MODULE;
	p->get_info = proc_scsi_devinfo_read;
	p->write_proc = proc_scsi_devinfo_write;
#endif /* CONFIG_SCSI_PROC_FS */

 out:
	if (error)
		scsi_exit_devinfo();
	return error;
}
