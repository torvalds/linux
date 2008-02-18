/*
 *  arch/s390/kernel/ipl.c
 *    ipl/reipl/dump support for Linux on s390.
 *
 *    Copyright IBM Corp. 2005,2007
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>
 *		 Volker Sameske <sameske@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/ctype.h>
#include <asm/ipl.h>
#include <asm/smp.h>
#include <asm/setup.h>
#include <asm/cpcmd.h>
#include <asm/cio.h>
#include <asm/ebcdic.h>
#include <asm/reset.h>
#include <asm/sclp.h>

#define IPL_PARM_BLOCK_VERSION 0

#define IPL_UNKNOWN_STR		"unknown"
#define IPL_CCW_STR		"ccw"
#define IPL_FCP_STR		"fcp"
#define IPL_FCP_DUMP_STR	"fcp_dump"
#define IPL_NSS_STR		"nss"

#define DUMP_CCW_STR		"ccw"
#define DUMP_FCP_STR		"fcp"
#define DUMP_NONE_STR		"none"

/*
 * Four shutdown trigger types are supported:
 * - panic
 * - halt
 * - power off
 * - reipl
 */
#define ON_PANIC_STR		"on_panic"
#define ON_HALT_STR		"on_halt"
#define ON_POFF_STR		"on_poff"
#define ON_REIPL_STR		"on_reboot"

struct shutdown_action;
struct shutdown_trigger {
	char *name;
	struct shutdown_action *action;
};

/*
 * Five shutdown action types are supported:
 */
#define SHUTDOWN_ACTION_IPL_STR		"ipl"
#define SHUTDOWN_ACTION_REIPL_STR	"reipl"
#define SHUTDOWN_ACTION_DUMP_STR	"dump"
#define SHUTDOWN_ACTION_VMCMD_STR	"vmcmd"
#define SHUTDOWN_ACTION_STOP_STR	"stop"

struct shutdown_action {
	char *name;
	void (*fn) (struct shutdown_trigger *trigger);
	int (*init) (void);
};

static char *ipl_type_str(enum ipl_type type)
{
	switch (type) {
	case IPL_TYPE_CCW:
		return IPL_CCW_STR;
	case IPL_TYPE_FCP:
		return IPL_FCP_STR;
	case IPL_TYPE_FCP_DUMP:
		return IPL_FCP_DUMP_STR;
	case IPL_TYPE_NSS:
		return IPL_NSS_STR;
	case IPL_TYPE_UNKNOWN:
	default:
		return IPL_UNKNOWN_STR;
	}
}

enum dump_type {
	DUMP_TYPE_NONE	= 1,
	DUMP_TYPE_CCW	= 2,
	DUMP_TYPE_FCP	= 4,
};

static char *dump_type_str(enum dump_type type)
{
	switch (type) {
	case DUMP_TYPE_NONE:
		return DUMP_NONE_STR;
	case DUMP_TYPE_CCW:
		return DUMP_CCW_STR;
	case DUMP_TYPE_FCP:
		return DUMP_FCP_STR;
	default:
		return NULL;
	}
}

/*
 * Must be in data section since the bss section
 * is not cleared when these are accessed.
 */
static u16 ipl_devno __attribute__((__section__(".data"))) = 0;
u32 ipl_flags __attribute__((__section__(".data"))) = 0;

enum ipl_method {
	REIPL_METHOD_CCW_CIO,
	REIPL_METHOD_CCW_DIAG,
	REIPL_METHOD_CCW_VM,
	REIPL_METHOD_FCP_RO_DIAG,
	REIPL_METHOD_FCP_RW_DIAG,
	REIPL_METHOD_FCP_RO_VM,
	REIPL_METHOD_FCP_DUMP,
	REIPL_METHOD_NSS,
	REIPL_METHOD_DEFAULT,
};

enum dump_method {
	DUMP_METHOD_NONE,
	DUMP_METHOD_CCW_CIO,
	DUMP_METHOD_CCW_DIAG,
	DUMP_METHOD_CCW_VM,
	DUMP_METHOD_FCP_DIAG,
};

static int diag308_set_works = 0;

static int reipl_capabilities = IPL_TYPE_UNKNOWN;

static enum ipl_type reipl_type = IPL_TYPE_UNKNOWN;
static enum ipl_method reipl_method = REIPL_METHOD_DEFAULT;
static struct ipl_parameter_block *reipl_block_fcp;
static struct ipl_parameter_block *reipl_block_ccw;

static char reipl_nss_name[NSS_NAME_SIZE + 1];

static int dump_capabilities = DUMP_TYPE_NONE;
static enum dump_type dump_type = DUMP_TYPE_NONE;
static enum dump_method dump_method = DUMP_METHOD_NONE;
static struct ipl_parameter_block *dump_block_fcp;
static struct ipl_parameter_block *dump_block_ccw;

static struct sclp_ipl_info sclp_ipl_info;

int diag308(unsigned long subcode, void *addr)
{
	register unsigned long _addr asm("0") = (unsigned long) addr;
	register unsigned long _rc asm("1") = 0;

	asm volatile(
		"	diag	%0,%2,0x308\n"
		"0:\n"
		EX_TABLE(0b,0b)
		: "+d" (_addr), "+d" (_rc)
		: "d" (subcode) : "cc", "memory");
	return _rc;
}
EXPORT_SYMBOL_GPL(diag308);

/* SYSFS */

#define DEFINE_IPL_ATTR_RO(_prefix, _name, _format, _value)		\
static ssize_t sys_##_prefix##_##_name##_show(struct kobject *kobj,	\
		struct kobj_attribute *attr,				\
		char *page)						\
{									\
	return sprintf(page, _format, _value);				\
}									\
static struct kobj_attribute sys_##_prefix##_##_name##_attr =		\
	__ATTR(_name, S_IRUGO, sys_##_prefix##_##_name##_show, NULL);

#define DEFINE_IPL_ATTR_RW(_prefix, _name, _fmt_out, _fmt_in, _value)	\
static ssize_t sys_##_prefix##_##_name##_show(struct kobject *kobj,	\
		struct kobj_attribute *attr,				\
		char *page)						\
{									\
	return sprintf(page, _fmt_out,					\
			(unsigned long long) _value);			\
}									\
static ssize_t sys_##_prefix##_##_name##_store(struct kobject *kobj,	\
		struct kobj_attribute *attr,				\
		const char *buf, size_t len)				\
{									\
	unsigned long long value;					\
	if (sscanf(buf, _fmt_in, &value) != 1)				\
		return -EINVAL;						\
	_value = value;							\
	return len;							\
}									\
static struct kobj_attribute sys_##_prefix##_##_name##_attr =		\
	__ATTR(_name,(S_IRUGO | S_IWUSR),				\
			sys_##_prefix##_##_name##_show,			\
			sys_##_prefix##_##_name##_store);

#define DEFINE_IPL_ATTR_STR_RW(_prefix, _name, _fmt_out, _fmt_in, _value)\
static ssize_t sys_##_prefix##_##_name##_show(struct kobject *kobj,	\
		struct kobj_attribute *attr,				\
		char *page)						\
{									\
	return sprintf(page, _fmt_out, _value);				\
}									\
static ssize_t sys_##_prefix##_##_name##_store(struct kobject *kobj,	\
		struct kobj_attribute *attr,				\
		const char *buf, size_t len)				\
{									\
	strncpy(_value, buf, sizeof(_value) - 1);			\
	strstrip(_value);						\
	return len;							\
}									\
static struct kobj_attribute sys_##_prefix##_##_name##_attr =		\
	__ATTR(_name,(S_IRUGO | S_IWUSR),				\
			sys_##_prefix##_##_name##_show,			\
			sys_##_prefix##_##_name##_store);

static void make_attrs_ro(struct attribute **attrs)
{
	while (*attrs) {
		(*attrs)->mode = S_IRUGO;
		attrs++;
	}
}

/*
 * ipl section
 */

static __init enum ipl_type get_ipl_type(void)
{
	struct ipl_parameter_block *ipl = IPL_PARMBLOCK_START;

	if (ipl_flags & IPL_NSS_VALID)
		return IPL_TYPE_NSS;
	if (!(ipl_flags & IPL_DEVNO_VALID))
		return IPL_TYPE_UNKNOWN;
	if (!(ipl_flags & IPL_PARMBLOCK_VALID))
		return IPL_TYPE_CCW;
	if (ipl->hdr.version > IPL_MAX_SUPPORTED_VERSION)
		return IPL_TYPE_UNKNOWN;
	if (ipl->hdr.pbt != DIAG308_IPL_TYPE_FCP)
		return IPL_TYPE_UNKNOWN;
	if (ipl->ipl_info.fcp.opt == DIAG308_IPL_OPT_DUMP)
		return IPL_TYPE_FCP_DUMP;
	return IPL_TYPE_FCP;
}

struct ipl_info ipl_info;
EXPORT_SYMBOL_GPL(ipl_info);

static ssize_t ipl_type_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *page)
{
	return sprintf(page, "%s\n", ipl_type_str(ipl_info.type));
}

static struct kobj_attribute sys_ipl_type_attr = __ATTR_RO(ipl_type);

static ssize_t sys_ipl_device_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *page)
{
	struct ipl_parameter_block *ipl = IPL_PARMBLOCK_START;

	switch (ipl_info.type) {
	case IPL_TYPE_CCW:
		return sprintf(page, "0.0.%04x\n", ipl_devno);
	case IPL_TYPE_FCP:
	case IPL_TYPE_FCP_DUMP:
		return sprintf(page, "0.0.%04x\n", ipl->ipl_info.fcp.devno);
	default:
		return 0;
	}
}

static struct kobj_attribute sys_ipl_device_attr =
	__ATTR(device, S_IRUGO, sys_ipl_device_show, NULL);

static ssize_t ipl_parameter_read(struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	unsigned int size = IPL_PARMBLOCK_SIZE;

	if (off > size)
		return 0;
	if (off + count > size)
		count = size - off;
	memcpy(buf, (void *)IPL_PARMBLOCK_START + off, count);
	return count;
}

static struct bin_attribute ipl_parameter_attr = {
	.attr = {
		.name = "binary_parameter",
		.mode = S_IRUGO,
	},
	.size = PAGE_SIZE,
	.read = &ipl_parameter_read,
};

static ssize_t ipl_scp_data_read(struct kobject *kobj, struct bin_attribute *attr,
				 char *buf, loff_t off, size_t count)
{
	unsigned int size = IPL_PARMBLOCK_START->ipl_info.fcp.scp_data_len;
	void *scp_data = &IPL_PARMBLOCK_START->ipl_info.fcp.scp_data;

	if (off > size)
		return 0;
	if (off + count > size)
		count = size - off;
	memcpy(buf, scp_data + off, count);
	return count;
}

static struct bin_attribute ipl_scp_data_attr = {
	.attr = {
		.name = "scp_data",
		.mode = S_IRUGO,
	},
	.size = PAGE_SIZE,
	.read = ipl_scp_data_read,
};

/* FCP ipl device attributes */

DEFINE_IPL_ATTR_RO(ipl_fcp, wwpn, "0x%016llx\n", (unsigned long long)
		   IPL_PARMBLOCK_START->ipl_info.fcp.wwpn);
DEFINE_IPL_ATTR_RO(ipl_fcp, lun, "0x%016llx\n", (unsigned long long)
		   IPL_PARMBLOCK_START->ipl_info.fcp.lun);
DEFINE_IPL_ATTR_RO(ipl_fcp, bootprog, "%lld\n", (unsigned long long)
		   IPL_PARMBLOCK_START->ipl_info.fcp.bootprog);
DEFINE_IPL_ATTR_RO(ipl_fcp, br_lba, "%lld\n", (unsigned long long)
		   IPL_PARMBLOCK_START->ipl_info.fcp.br_lba);

static struct attribute *ipl_fcp_attrs[] = {
	&sys_ipl_type_attr.attr,
	&sys_ipl_device_attr.attr,
	&sys_ipl_fcp_wwpn_attr.attr,
	&sys_ipl_fcp_lun_attr.attr,
	&sys_ipl_fcp_bootprog_attr.attr,
	&sys_ipl_fcp_br_lba_attr.attr,
	NULL,
};

static struct attribute_group ipl_fcp_attr_group = {
	.attrs = ipl_fcp_attrs,
};

/* CCW ipl device attributes */

static ssize_t ipl_ccw_loadparm_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *page)
{
	char loadparm[LOADPARM_LEN + 1] = {};

	if (!sclp_ipl_info.is_valid)
		return sprintf(page, "#unknown#\n");
	memcpy(loadparm, &sclp_ipl_info.loadparm, LOADPARM_LEN);
	EBCASC(loadparm, LOADPARM_LEN);
	strstrip(loadparm);
	return sprintf(page, "%s\n", loadparm);
}

static struct kobj_attribute sys_ipl_ccw_loadparm_attr =
	__ATTR(loadparm, 0444, ipl_ccw_loadparm_show, NULL);

static struct attribute *ipl_ccw_attrs[] = {
	&sys_ipl_type_attr.attr,
	&sys_ipl_device_attr.attr,
	&sys_ipl_ccw_loadparm_attr.attr,
	NULL,
};

static struct attribute_group ipl_ccw_attr_group = {
	.attrs = ipl_ccw_attrs,
};

/* NSS ipl device attributes */

DEFINE_IPL_ATTR_RO(ipl_nss, name, "%s\n", kernel_nss_name);

static struct attribute *ipl_nss_attrs[] = {
	&sys_ipl_type_attr.attr,
	&sys_ipl_nss_name_attr.attr,
	NULL,
};

static struct attribute_group ipl_nss_attr_group = {
	.attrs = ipl_nss_attrs,
};

/* UNKNOWN ipl device attributes */

static struct attribute *ipl_unknown_attrs[] = {
	&sys_ipl_type_attr.attr,
	NULL,
};

static struct attribute_group ipl_unknown_attr_group = {
	.attrs = ipl_unknown_attrs,
};

static struct kset *ipl_kset;

static int __init ipl_register_fcp_files(void)
{
	int rc;

	rc = sysfs_create_group(&ipl_kset->kobj, &ipl_fcp_attr_group);
	if (rc)
		goto out;
	rc = sysfs_create_bin_file(&ipl_kset->kobj, &ipl_parameter_attr);
	if (rc)
		goto out_ipl_parm;
	rc = sysfs_create_bin_file(&ipl_kset->kobj, &ipl_scp_data_attr);
	if (!rc)
		goto out;

	sysfs_remove_bin_file(&ipl_kset->kobj, &ipl_parameter_attr);

out_ipl_parm:
	sysfs_remove_group(&ipl_kset->kobj, &ipl_fcp_attr_group);
out:
	return rc;
}

static void ipl_run(struct shutdown_trigger *trigger)
{
	diag308(DIAG308_IPL, NULL);
	if (MACHINE_IS_VM)
		__cpcmd("IPL", NULL, 0, NULL);
	else if (ipl_info.type == IPL_TYPE_CCW)
		reipl_ccw_dev(&ipl_info.data.ccw.dev_id);
}

static int __init ipl_init(void)
{
	int rc;

	ipl_kset = kset_create_and_add("ipl", NULL, firmware_kobj);
	if (!ipl_kset) {
		rc = -ENOMEM;
		goto out;
	}
	switch (ipl_info.type) {
	case IPL_TYPE_CCW:
		rc = sysfs_create_group(&ipl_kset->kobj, &ipl_ccw_attr_group);
		break;
	case IPL_TYPE_FCP:
	case IPL_TYPE_FCP_DUMP:
		rc = ipl_register_fcp_files();
		break;
	case IPL_TYPE_NSS:
		rc = sysfs_create_group(&ipl_kset->kobj, &ipl_nss_attr_group);
		break;
	default:
		rc = sysfs_create_group(&ipl_kset->kobj,
					&ipl_unknown_attr_group);
		break;
	}
out:
	if (rc)
		panic("ipl_init failed: rc = %i\n", rc);

	return 0;
}

static struct shutdown_action __refdata ipl_action = {
	.name	= SHUTDOWN_ACTION_IPL_STR,
	.fn	= ipl_run,
	.init	= ipl_init,
};

/*
 * reipl shutdown action: Reboot Linux on shutdown.
 */

/* FCP reipl device attributes */

DEFINE_IPL_ATTR_RW(reipl_fcp, wwpn, "0x%016llx\n", "%016llx\n",
		   reipl_block_fcp->ipl_info.fcp.wwpn);
DEFINE_IPL_ATTR_RW(reipl_fcp, lun, "0x%016llx\n", "%016llx\n",
		   reipl_block_fcp->ipl_info.fcp.lun);
DEFINE_IPL_ATTR_RW(reipl_fcp, bootprog, "%lld\n", "%lld\n",
		   reipl_block_fcp->ipl_info.fcp.bootprog);
DEFINE_IPL_ATTR_RW(reipl_fcp, br_lba, "%lld\n", "%lld\n",
		   reipl_block_fcp->ipl_info.fcp.br_lba);
DEFINE_IPL_ATTR_RW(reipl_fcp, device, "0.0.%04llx\n", "0.0.%llx\n",
		   reipl_block_fcp->ipl_info.fcp.devno);

static struct attribute *reipl_fcp_attrs[] = {
	&sys_reipl_fcp_device_attr.attr,
	&sys_reipl_fcp_wwpn_attr.attr,
	&sys_reipl_fcp_lun_attr.attr,
	&sys_reipl_fcp_bootprog_attr.attr,
	&sys_reipl_fcp_br_lba_attr.attr,
	NULL,
};

static struct attribute_group reipl_fcp_attr_group = {
	.name  = IPL_FCP_STR,
	.attrs = reipl_fcp_attrs,
};

/* CCW reipl device attributes */

DEFINE_IPL_ATTR_RW(reipl_ccw, device, "0.0.%04llx\n", "0.0.%llx\n",
	reipl_block_ccw->ipl_info.ccw.devno);

static void reipl_get_ascii_loadparm(char *loadparm)
{
	memcpy(loadparm, &reipl_block_ccw->ipl_info.ccw.load_param,
	       LOADPARM_LEN);
	EBCASC(loadparm, LOADPARM_LEN);
	loadparm[LOADPARM_LEN] = 0;
	strstrip(loadparm);
}

static ssize_t reipl_ccw_loadparm_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *page)
{
	char buf[LOADPARM_LEN + 1];

	reipl_get_ascii_loadparm(buf);
	return sprintf(page, "%s\n", buf);
}

static ssize_t reipl_ccw_loadparm_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t len)
{
	int i, lp_len;

	/* ignore trailing newline */
	lp_len = len;
	if ((len > 0) && (buf[len - 1] == '\n'))
		lp_len--;
	/* loadparm can have max 8 characters and must not start with a blank */
	if ((lp_len > LOADPARM_LEN) || ((lp_len > 0) && (buf[0] == ' ')))
		return -EINVAL;
	/* loadparm can only contain "a-z,A-Z,0-9,SP,." */
	for (i = 0; i < lp_len; i++) {
		if (isalpha(buf[i]) || isdigit(buf[i]) || (buf[i] == ' ') ||
		    (buf[i] == '.'))
			continue;
		return -EINVAL;
	}
	/* initialize loadparm with blanks */
	memset(&reipl_block_ccw->ipl_info.ccw.load_param, ' ', LOADPARM_LEN);
	/* copy and convert to ebcdic */
	memcpy(&reipl_block_ccw->ipl_info.ccw.load_param, buf, lp_len);
	ASCEBC(reipl_block_ccw->ipl_info.ccw.load_param, LOADPARM_LEN);
	return len;
}

static struct kobj_attribute sys_reipl_ccw_loadparm_attr =
	__ATTR(loadparm, 0644, reipl_ccw_loadparm_show,
	       reipl_ccw_loadparm_store);

static struct attribute *reipl_ccw_attrs[] = {
	&sys_reipl_ccw_device_attr.attr,
	&sys_reipl_ccw_loadparm_attr.attr,
	NULL,
};

static struct attribute_group reipl_ccw_attr_group = {
	.name  = IPL_CCW_STR,
	.attrs = reipl_ccw_attrs,
};


/* NSS reipl device attributes */

DEFINE_IPL_ATTR_STR_RW(reipl_nss, name, "%s\n", "%s\n", reipl_nss_name);

static struct attribute *reipl_nss_attrs[] = {
	&sys_reipl_nss_name_attr.attr,
	NULL,
};

static struct attribute_group reipl_nss_attr_group = {
	.name  = IPL_NSS_STR,
	.attrs = reipl_nss_attrs,
};

/* reipl type */

static int reipl_set_type(enum ipl_type type)
{
	if (!(reipl_capabilities & type))
		return -EINVAL;

	switch(type) {
	case IPL_TYPE_CCW:
		if (diag308_set_works)
			reipl_method = REIPL_METHOD_CCW_DIAG;
		else if (MACHINE_IS_VM)
			reipl_method = REIPL_METHOD_CCW_VM;
		else
			reipl_method = REIPL_METHOD_CCW_CIO;
		break;
	case IPL_TYPE_FCP:
		if (diag308_set_works)
			reipl_method = REIPL_METHOD_FCP_RW_DIAG;
		else if (MACHINE_IS_VM)
			reipl_method = REIPL_METHOD_FCP_RO_VM;
		else
			reipl_method = REIPL_METHOD_FCP_RO_DIAG;
		break;
	case IPL_TYPE_FCP_DUMP:
		reipl_method = REIPL_METHOD_FCP_DUMP;
		break;
	case IPL_TYPE_NSS:
		reipl_method = REIPL_METHOD_NSS;
		break;
	case IPL_TYPE_UNKNOWN:
		reipl_method = REIPL_METHOD_DEFAULT;
		break;
	default:
		BUG();
	}
	reipl_type = type;
	return 0;
}

static ssize_t reipl_type_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *page)
{
	return sprintf(page, "%s\n", ipl_type_str(reipl_type));
}

static ssize_t reipl_type_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t len)
{
	int rc = -EINVAL;

	if (strncmp(buf, IPL_CCW_STR, strlen(IPL_CCW_STR)) == 0)
		rc = reipl_set_type(IPL_TYPE_CCW);
	else if (strncmp(buf, IPL_FCP_STR, strlen(IPL_FCP_STR)) == 0)
		rc = reipl_set_type(IPL_TYPE_FCP);
	else if (strncmp(buf, IPL_NSS_STR, strlen(IPL_NSS_STR)) == 0)
		rc = reipl_set_type(IPL_TYPE_NSS);
	return (rc != 0) ? rc : len;
}

static struct kobj_attribute reipl_type_attr =
	__ATTR(reipl_type, 0644, reipl_type_show, reipl_type_store);

static struct kset *reipl_kset;

void reipl_run(struct shutdown_trigger *trigger)
{
	struct ccw_dev_id devid;
	static char buf[100];
	char loadparm[LOADPARM_LEN + 1];

	switch (reipl_method) {
	case REIPL_METHOD_CCW_CIO:
		devid.devno = reipl_block_ccw->ipl_info.ccw.devno;
		devid.ssid  = 0;
		reipl_ccw_dev(&devid);
		break;
	case REIPL_METHOD_CCW_VM:
		reipl_get_ascii_loadparm(loadparm);
		if (strlen(loadparm) == 0)
			sprintf(buf, "IPL %X CLEAR",
				reipl_block_ccw->ipl_info.ccw.devno);
		else
			sprintf(buf, "IPL %X CLEAR LOADPARM '%s'",
				reipl_block_ccw->ipl_info.ccw.devno, loadparm);
		__cpcmd(buf, NULL, 0, NULL);
		break;
	case REIPL_METHOD_CCW_DIAG:
		diag308(DIAG308_SET, reipl_block_ccw);
		diag308(DIAG308_IPL, NULL);
		break;
	case REIPL_METHOD_FCP_RW_DIAG:
		diag308(DIAG308_SET, reipl_block_fcp);
		diag308(DIAG308_IPL, NULL);
		break;
	case REIPL_METHOD_FCP_RO_DIAG:
		diag308(DIAG308_IPL, NULL);
		break;
	case REIPL_METHOD_FCP_RO_VM:
		__cpcmd("IPL", NULL, 0, NULL);
		break;
	case REIPL_METHOD_NSS:
		sprintf(buf, "IPL %s", reipl_nss_name);
		__cpcmd(buf, NULL, 0, NULL);
		break;
	case REIPL_METHOD_DEFAULT:
		if (MACHINE_IS_VM)
			__cpcmd("IPL", NULL, 0, NULL);
		diag308(DIAG308_IPL, NULL);
		break;
	case REIPL_METHOD_FCP_DUMP:
	default:
		break;
	}
}

static void __init reipl_probe(void)
{
	void *buffer;

	buffer = (void *) get_zeroed_page(GFP_KERNEL);
	if (!buffer)
		return;
	if (diag308(DIAG308_STORE, buffer) == DIAG308_RC_OK)
		diag308_set_works = 1;
	free_page((unsigned long)buffer);
}

static int __init reipl_nss_init(void)
{
	int rc;

	if (!MACHINE_IS_VM)
		return 0;
	rc = sysfs_create_group(&reipl_kset->kobj, &reipl_nss_attr_group);
	if (rc)
		return rc;
	strncpy(reipl_nss_name, kernel_nss_name, NSS_NAME_SIZE + 1);
	reipl_capabilities |= IPL_TYPE_NSS;
	return 0;
}

static int __init reipl_ccw_init(void)
{
	int rc;

	reipl_block_ccw = (void *) get_zeroed_page(GFP_KERNEL);
	if (!reipl_block_ccw)
		return -ENOMEM;
	rc = sysfs_create_group(&reipl_kset->kobj, &reipl_ccw_attr_group);
	if (rc) {
		free_page((unsigned long)reipl_block_ccw);
		return rc;
	}
	reipl_block_ccw->hdr.len = IPL_PARM_BLK_CCW_LEN;
	reipl_block_ccw->hdr.version = IPL_PARM_BLOCK_VERSION;
	reipl_block_ccw->hdr.blk0_len = IPL_PARM_BLK0_CCW_LEN;
	reipl_block_ccw->hdr.pbt = DIAG308_IPL_TYPE_CCW;
	reipl_block_ccw->hdr.flags = DIAG308_FLAGS_LP_VALID;
	/* check if read scp info worked and set loadparm */
	if (sclp_ipl_info.is_valid)
		memcpy(reipl_block_ccw->ipl_info.ccw.load_param,
		       &sclp_ipl_info.loadparm, LOADPARM_LEN);
	else
		/* read scp info failed: set empty loadparm (EBCDIC blanks) */
		memset(reipl_block_ccw->ipl_info.ccw.load_param, 0x40,
		       LOADPARM_LEN);
	if (!MACHINE_IS_VM && !diag308_set_works)
		sys_reipl_ccw_loadparm_attr.attr.mode = S_IRUGO;
	if (ipl_info.type == IPL_TYPE_CCW)
		reipl_block_ccw->ipl_info.ccw.devno = ipl_devno;
	reipl_capabilities |= IPL_TYPE_CCW;
	return 0;
}

static int __init reipl_fcp_init(void)
{
	int rc;

	if ((!diag308_set_works) && (ipl_info.type != IPL_TYPE_FCP))
		return 0;
	if ((!diag308_set_works) && (ipl_info.type == IPL_TYPE_FCP))
		make_attrs_ro(reipl_fcp_attrs);

	reipl_block_fcp = (void *) get_zeroed_page(GFP_KERNEL);
	if (!reipl_block_fcp)
		return -ENOMEM;
	rc = sysfs_create_group(&reipl_kset->kobj, &reipl_fcp_attr_group);
	if (rc) {
		free_page((unsigned long)reipl_block_fcp);
		return rc;
	}
	if (ipl_info.type == IPL_TYPE_FCP) {
		memcpy(reipl_block_fcp, IPL_PARMBLOCK_START, PAGE_SIZE);
	} else {
		reipl_block_fcp->hdr.len = IPL_PARM_BLK_FCP_LEN;
		reipl_block_fcp->hdr.version = IPL_PARM_BLOCK_VERSION;
		reipl_block_fcp->hdr.blk0_len = IPL_PARM_BLK0_FCP_LEN;
		reipl_block_fcp->hdr.pbt = DIAG308_IPL_TYPE_FCP;
		reipl_block_fcp->ipl_info.fcp.opt = DIAG308_IPL_OPT_IPL;
	}
	reipl_capabilities |= IPL_TYPE_FCP;
	return 0;
}

static int __init reipl_init(void)
{
	int rc;

	reipl_kset = kset_create_and_add("reipl", NULL, firmware_kobj);
	if (!reipl_kset)
		return -ENOMEM;
	rc = sysfs_create_file(&reipl_kset->kobj, &reipl_type_attr.attr);
	if (rc) {
		kset_unregister(reipl_kset);
		return rc;
	}
	rc = reipl_ccw_init();
	if (rc)
		return rc;
	rc = reipl_fcp_init();
	if (rc)
		return rc;
	rc = reipl_nss_init();
	if (rc)
		return rc;
	rc = reipl_set_type(ipl_info.type);
	if (rc)
		return rc;
	return 0;
}

static struct shutdown_action __refdata reipl_action = {
	.name	= SHUTDOWN_ACTION_REIPL_STR,
	.fn	= reipl_run,
	.init	= reipl_init,
};

/*
 * dump shutdown action: Dump Linux on shutdown.
 */

/* FCP dump device attributes */

DEFINE_IPL_ATTR_RW(dump_fcp, wwpn, "0x%016llx\n", "%016llx\n",
		   dump_block_fcp->ipl_info.fcp.wwpn);
DEFINE_IPL_ATTR_RW(dump_fcp, lun, "0x%016llx\n", "%016llx\n",
		   dump_block_fcp->ipl_info.fcp.lun);
DEFINE_IPL_ATTR_RW(dump_fcp, bootprog, "%lld\n", "%lld\n",
		   dump_block_fcp->ipl_info.fcp.bootprog);
DEFINE_IPL_ATTR_RW(dump_fcp, br_lba, "%lld\n", "%lld\n",
		   dump_block_fcp->ipl_info.fcp.br_lba);
DEFINE_IPL_ATTR_RW(dump_fcp, device, "0.0.%04llx\n", "0.0.%llx\n",
		   dump_block_fcp->ipl_info.fcp.devno);

static struct attribute *dump_fcp_attrs[] = {
	&sys_dump_fcp_device_attr.attr,
	&sys_dump_fcp_wwpn_attr.attr,
	&sys_dump_fcp_lun_attr.attr,
	&sys_dump_fcp_bootprog_attr.attr,
	&sys_dump_fcp_br_lba_attr.attr,
	NULL,
};

static struct attribute_group dump_fcp_attr_group = {
	.name  = IPL_FCP_STR,
	.attrs = dump_fcp_attrs,
};

/* CCW dump device attributes */

DEFINE_IPL_ATTR_RW(dump_ccw, device, "0.0.%04llx\n", "0.0.%llx\n",
		   dump_block_ccw->ipl_info.ccw.devno);

static struct attribute *dump_ccw_attrs[] = {
	&sys_dump_ccw_device_attr.attr,
	NULL,
};

static struct attribute_group dump_ccw_attr_group = {
	.name  = IPL_CCW_STR,
	.attrs = dump_ccw_attrs,
};

/* dump type */

static int dump_set_type(enum dump_type type)
{
	if (!(dump_capabilities & type))
		return -EINVAL;
	switch (type) {
	case DUMP_TYPE_CCW:
		if (diag308_set_works)
			dump_method = DUMP_METHOD_CCW_DIAG;
		else if (MACHINE_IS_VM)
			dump_method = DUMP_METHOD_CCW_VM;
		else
			dump_method = DUMP_METHOD_CCW_CIO;
		break;
	case DUMP_TYPE_FCP:
		dump_method = DUMP_METHOD_FCP_DIAG;
		break;
	default:
		dump_method = DUMP_METHOD_NONE;
	}
	dump_type = type;
	return 0;
}

static ssize_t dump_type_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *page)
{
	return sprintf(page, "%s\n", dump_type_str(dump_type));
}

static ssize_t dump_type_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t len)
{
	int rc = -EINVAL;

	if (strncmp(buf, DUMP_NONE_STR, strlen(DUMP_NONE_STR)) == 0)
		rc = dump_set_type(DUMP_TYPE_NONE);
	else if (strncmp(buf, DUMP_CCW_STR, strlen(DUMP_CCW_STR)) == 0)
		rc = dump_set_type(DUMP_TYPE_CCW);
	else if (strncmp(buf, DUMP_FCP_STR, strlen(DUMP_FCP_STR)) == 0)
		rc = dump_set_type(DUMP_TYPE_FCP);
	return (rc != 0) ? rc : len;
}

static struct kobj_attribute dump_type_attr =
	__ATTR(dump_type, 0644, dump_type_show, dump_type_store);

static struct kset *dump_kset;

static void dump_run(struct shutdown_trigger *trigger)
{
	struct ccw_dev_id devid;
	static char buf[100];

	switch (dump_method) {
	case DUMP_METHOD_CCW_CIO:
		smp_send_stop();
		devid.devno = dump_block_ccw->ipl_info.ccw.devno;
		devid.ssid  = 0;
		reipl_ccw_dev(&devid);
		break;
	case DUMP_METHOD_CCW_VM:
		smp_send_stop();
		sprintf(buf, "STORE STATUS");
		__cpcmd(buf, NULL, 0, NULL);
		sprintf(buf, "IPL %X", dump_block_ccw->ipl_info.ccw.devno);
		__cpcmd(buf, NULL, 0, NULL);
		break;
	case DUMP_METHOD_CCW_DIAG:
		diag308(DIAG308_SET, dump_block_ccw);
		diag308(DIAG308_DUMP, NULL);
		break;
	case DUMP_METHOD_FCP_DIAG:
		diag308(DIAG308_SET, dump_block_fcp);
		diag308(DIAG308_DUMP, NULL);
		break;
	case DUMP_METHOD_NONE:
	default:
		return;
	}
	printk(KERN_EMERG "Dump failed!\n");
}

static int __init dump_ccw_init(void)
{
	int rc;

	dump_block_ccw = (void *) get_zeroed_page(GFP_KERNEL);
	if (!dump_block_ccw)
		return -ENOMEM;
	rc = sysfs_create_group(&dump_kset->kobj, &dump_ccw_attr_group);
	if (rc) {
		free_page((unsigned long)dump_block_ccw);
		return rc;
	}
	dump_block_ccw->hdr.len = IPL_PARM_BLK_CCW_LEN;
	dump_block_ccw->hdr.version = IPL_PARM_BLOCK_VERSION;
	dump_block_ccw->hdr.blk0_len = IPL_PARM_BLK0_CCW_LEN;
	dump_block_ccw->hdr.pbt = DIAG308_IPL_TYPE_CCW;
	dump_capabilities |= DUMP_TYPE_CCW;
	return 0;
}

static int __init dump_fcp_init(void)
{
	int rc;

	if (!sclp_ipl_info.has_dump)
		return 0; /* LDIPL DUMP is not installed */
	if (!diag308_set_works)
		return 0;
	dump_block_fcp = (void *) get_zeroed_page(GFP_KERNEL);
	if (!dump_block_fcp)
		return -ENOMEM;
	rc = sysfs_create_group(&dump_kset->kobj, &dump_fcp_attr_group);
	if (rc) {
		free_page((unsigned long)dump_block_fcp);
		return rc;
	}
	dump_block_fcp->hdr.len = IPL_PARM_BLK_FCP_LEN;
	dump_block_fcp->hdr.version = IPL_PARM_BLOCK_VERSION;
	dump_block_fcp->hdr.blk0_len = IPL_PARM_BLK0_FCP_LEN;
	dump_block_fcp->hdr.pbt = DIAG308_IPL_TYPE_FCP;
	dump_block_fcp->ipl_info.fcp.opt = DIAG308_IPL_OPT_DUMP;
	dump_capabilities |= DUMP_TYPE_FCP;
	return 0;
}

static int __init dump_init(void)
{
	int rc;

	dump_kset = kset_create_and_add("dump", NULL, firmware_kobj);
	if (!dump_kset)
		return -ENOMEM;
	rc = sysfs_create_file(&dump_kset->kobj, &dump_type_attr.attr);
	if (rc) {
		kset_unregister(dump_kset);
		return rc;
	}
	rc = dump_ccw_init();
	if (rc)
		return rc;
	rc = dump_fcp_init();
	if (rc)
		return rc;
	dump_set_type(DUMP_TYPE_NONE);
	return 0;
}

static struct shutdown_action __refdata dump_action = {
	.name	= SHUTDOWN_ACTION_DUMP_STR,
	.fn	= dump_run,
	.init	= dump_init,
};

/*
 * vmcmd shutdown action: Trigger vm command on shutdown.
 */

static char vmcmd_on_reboot[128];
static char vmcmd_on_panic[128];
static char vmcmd_on_halt[128];
static char vmcmd_on_poff[128];

DEFINE_IPL_ATTR_STR_RW(vmcmd, on_reboot, "%s\n", "%s\n", vmcmd_on_reboot);
DEFINE_IPL_ATTR_STR_RW(vmcmd, on_panic, "%s\n", "%s\n", vmcmd_on_panic);
DEFINE_IPL_ATTR_STR_RW(vmcmd, on_halt, "%s\n", "%s\n", vmcmd_on_halt);
DEFINE_IPL_ATTR_STR_RW(vmcmd, on_poff, "%s\n", "%s\n", vmcmd_on_poff);

static struct attribute *vmcmd_attrs[] = {
	&sys_vmcmd_on_reboot_attr.attr,
	&sys_vmcmd_on_panic_attr.attr,
	&sys_vmcmd_on_halt_attr.attr,
	&sys_vmcmd_on_poff_attr.attr,
	NULL,
};

static struct attribute_group vmcmd_attr_group = {
	.attrs = vmcmd_attrs,
};

static struct kset *vmcmd_kset;

static void vmcmd_run(struct shutdown_trigger *trigger)
{
	char *cmd, *next_cmd;

	if (strcmp(trigger->name, ON_REIPL_STR) == 0)
		cmd = vmcmd_on_reboot;
	else if (strcmp(trigger->name, ON_PANIC_STR) == 0)
		cmd = vmcmd_on_panic;
	else if (strcmp(trigger->name, ON_HALT_STR) == 0)
		cmd = vmcmd_on_halt;
	else if (strcmp(trigger->name, ON_POFF_STR) == 0)
		cmd = vmcmd_on_poff;
	else
		return;

	if (strlen(cmd) == 0)
		return;
	do {
		next_cmd = strchr(cmd, '\n');
		if (next_cmd) {
			next_cmd[0] = 0;
			next_cmd += 1;
		}
		__cpcmd(cmd, NULL, 0, NULL);
		cmd = next_cmd;
	} while (cmd != NULL);
}

static int vmcmd_init(void)
{
	if (!MACHINE_IS_VM)
		return -ENOTSUPP;
	vmcmd_kset = kset_create_and_add("vmcmd", NULL, firmware_kobj);
	if (!vmcmd_kset)
		return -ENOMEM;
	return sysfs_create_group(&vmcmd_kset->kobj, &vmcmd_attr_group);
}

static struct shutdown_action vmcmd_action = {SHUTDOWN_ACTION_VMCMD_STR,
					      vmcmd_run, vmcmd_init};

/*
 * stop shutdown action: Stop Linux on shutdown.
 */

static void stop_run(struct shutdown_trigger *trigger)
{
	if (strcmp(trigger->name, ON_PANIC_STR) == 0)
		disabled_wait((unsigned long) __builtin_return_address(0));
	else {
		signal_processor(smp_processor_id(), sigp_stop);
		for (;;);
	}
}

static struct shutdown_action stop_action = {SHUTDOWN_ACTION_STOP_STR,
					     stop_run, NULL};

/* action list */

static struct shutdown_action *shutdown_actions_list[] = {
	&ipl_action, &reipl_action, &dump_action, &vmcmd_action, &stop_action};
#define SHUTDOWN_ACTIONS_COUNT (sizeof(shutdown_actions_list) / sizeof(void *))

/*
 * Trigger section
 */

static struct kset *shutdown_actions_kset;

static int set_trigger(const char *buf, struct shutdown_trigger *trigger,
		       size_t len)
{
	int i;
	for (i = 0; i < SHUTDOWN_ACTIONS_COUNT; i++) {
		if (!shutdown_actions_list[i])
			continue;
		if (strncmp(buf, shutdown_actions_list[i]->name,
			    strlen(shutdown_actions_list[i]->name)) == 0) {
			trigger->action = shutdown_actions_list[i];
			return len;
		}
	}
	return -EINVAL;
}

/* on reipl */

static struct shutdown_trigger on_reboot_trigger = {ON_REIPL_STR,
						    &reipl_action};

static ssize_t on_reboot_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *page)
{
	return sprintf(page, "%s\n", on_reboot_trigger.action->name);
}

static ssize_t on_reboot_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t len)
{
	return set_trigger(buf, &on_reboot_trigger, len);
}

static struct kobj_attribute on_reboot_attr =
	__ATTR(on_reboot, 0644, on_reboot_show, on_reboot_store);

static void do_machine_restart(char *__unused)
{
	smp_send_stop();
	on_reboot_trigger.action->fn(&on_reboot_trigger);
	reipl_run(NULL);
}
void (*_machine_restart)(char *command) = do_machine_restart;

/* on panic */

static struct shutdown_trigger on_panic_trigger = {ON_PANIC_STR, &stop_action};

static ssize_t on_panic_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *page)
{
	return sprintf(page, "%s\n", on_panic_trigger.action->name);
}

static ssize_t on_panic_store(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      const char *buf, size_t len)
{
	return set_trigger(buf, &on_panic_trigger, len);
}

static struct kobj_attribute on_panic_attr =
	__ATTR(on_panic, 0644, on_panic_show, on_panic_store);

static void do_panic(void)
{
	on_panic_trigger.action->fn(&on_panic_trigger);
	stop_run(&on_panic_trigger);
}

/* on halt */

static struct shutdown_trigger on_halt_trigger = {ON_HALT_STR, &stop_action};

static ssize_t on_halt_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *page)
{
	return sprintf(page, "%s\n", on_halt_trigger.action->name);
}

static ssize_t on_halt_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t len)
{
	return set_trigger(buf, &on_halt_trigger, len);
}

static struct kobj_attribute on_halt_attr =
	__ATTR(on_halt, 0644, on_halt_show, on_halt_store);


static void do_machine_halt(void)
{
	smp_send_stop();
	on_halt_trigger.action->fn(&on_halt_trigger);
	stop_run(&on_halt_trigger);
}
void (*_machine_halt)(void) = do_machine_halt;

/* on power off */

static struct shutdown_trigger on_poff_trigger = {ON_POFF_STR, &stop_action};

static ssize_t on_poff_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *page)
{
	return sprintf(page, "%s\n", on_poff_trigger.action->name);
}

static ssize_t on_poff_store(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t len)
{
	return set_trigger(buf, &on_poff_trigger, len);
}

static struct kobj_attribute on_poff_attr =
	__ATTR(on_poff, 0644, on_poff_show, on_poff_store);


static void do_machine_power_off(void)
{
	smp_send_stop();
	on_poff_trigger.action->fn(&on_poff_trigger);
	stop_run(&on_poff_trigger);
}
void (*_machine_power_off)(void) = do_machine_power_off;

static void __init shutdown_triggers_init(void)
{
	shutdown_actions_kset = kset_create_and_add("shutdown_actions", NULL,
						    firmware_kobj);
	if (!shutdown_actions_kset)
		goto fail;
	if (sysfs_create_file(&shutdown_actions_kset->kobj,
			      &on_reboot_attr.attr))
		goto fail;
	if (sysfs_create_file(&shutdown_actions_kset->kobj,
			      &on_panic_attr.attr))
		goto fail;
	if (sysfs_create_file(&shutdown_actions_kset->kobj,
			      &on_halt_attr.attr))
		goto fail;
	if (sysfs_create_file(&shutdown_actions_kset->kobj,
			      &on_poff_attr.attr))
		goto fail;

	return;
fail:
	panic("shutdown_triggers_init failed\n");
}

static void __init shutdown_actions_init(void)
{
	int i;

	for (i = 0; i < SHUTDOWN_ACTIONS_COUNT; i++) {
		if (!shutdown_actions_list[i]->init)
			continue;
		if (shutdown_actions_list[i]->init())
			shutdown_actions_list[i] = NULL;
	}
}

static int __init s390_ipl_init(void)
{
	reipl_probe();
	sclp_get_ipl_info(&sclp_ipl_info);
	shutdown_actions_init();
	shutdown_triggers_init();
	return 0;
}

__initcall(s390_ipl_init);

static void __init strncpy_skip_quote(char *dst, char *src, int n)
{
	int sx, dx;

	dx = 0;
	for (sx = 0; src[sx] != 0; sx++) {
		if (src[sx] == '"')
			continue;
		dst[dx++] = src[sx];
		if (dx >= n)
			break;
	}
}

static int __init vmcmd_on_reboot_setup(char *str)
{
	if (!MACHINE_IS_VM)
		return 1;
	strncpy_skip_quote(vmcmd_on_reboot, str, 127);
	vmcmd_on_reboot[127] = 0;
	on_reboot_trigger.action = &vmcmd_action;
	return 1;
}
__setup("vmreboot=", vmcmd_on_reboot_setup);

static int __init vmcmd_on_panic_setup(char *str)
{
	if (!MACHINE_IS_VM)
		return 1;
	strncpy_skip_quote(vmcmd_on_panic, str, 127);
	vmcmd_on_panic[127] = 0;
	on_panic_trigger.action = &vmcmd_action;
	return 1;
}
__setup("vmpanic=", vmcmd_on_panic_setup);

static int __init vmcmd_on_halt_setup(char *str)
{
	if (!MACHINE_IS_VM)
		return 1;
	strncpy_skip_quote(vmcmd_on_halt, str, 127);
	vmcmd_on_halt[127] = 0;
	on_halt_trigger.action = &vmcmd_action;
	return 1;
}
__setup("vmhalt=", vmcmd_on_halt_setup);

static int __init vmcmd_on_poff_setup(char *str)
{
	if (!MACHINE_IS_VM)
		return 1;
	strncpy_skip_quote(vmcmd_on_poff, str, 127);
	vmcmd_on_poff[127] = 0;
	on_poff_trigger.action = &vmcmd_action;
	return 1;
}
__setup("vmpoff=", vmcmd_on_poff_setup);

static int on_panic_notify(struct notifier_block *self,
			   unsigned long event, void *data)
{
	do_panic();
	return NOTIFY_OK;
}

static struct notifier_block on_panic_nb = {
	.notifier_call = on_panic_notify,
	.priority = 0,
};

void __init setup_ipl(void)
{
	ipl_info.type = get_ipl_type();
	switch (ipl_info.type) {
	case IPL_TYPE_CCW:
		ipl_info.data.ccw.dev_id.devno = ipl_devno;
		ipl_info.data.ccw.dev_id.ssid = 0;
		break;
	case IPL_TYPE_FCP:
	case IPL_TYPE_FCP_DUMP:
		ipl_info.data.fcp.dev_id.devno =
			IPL_PARMBLOCK_START->ipl_info.fcp.devno;
		ipl_info.data.fcp.dev_id.ssid = 0;
		ipl_info.data.fcp.wwpn = IPL_PARMBLOCK_START->ipl_info.fcp.wwpn;
		ipl_info.data.fcp.lun = IPL_PARMBLOCK_START->ipl_info.fcp.lun;
		break;
	case IPL_TYPE_NSS:
		strncpy(ipl_info.data.nss.name, kernel_nss_name,
			sizeof(ipl_info.data.nss.name));
		break;
	case IPL_TYPE_UNKNOWN:
	default:
		/* We have no info to copy */
		break;
	}
	atomic_notifier_chain_register(&panic_notifier_list, &on_panic_nb);
}

void __init ipl_save_parameters(void)
{
	struct cio_iplinfo iplinfo;
	unsigned int *ipl_ptr;
	void *src, *dst;

	if (cio_get_iplinfo(&iplinfo))
		return;

	ipl_devno = iplinfo.devno;
	ipl_flags |= IPL_DEVNO_VALID;
	if (!iplinfo.is_qdio)
		return;
	ipl_flags |= IPL_PARMBLOCK_VALID;
	ipl_ptr = (unsigned int *)__LC_IPL_PARMBLOCK_PTR;
	src = (void *)(unsigned long)*ipl_ptr;
	dst = (void *)IPL_PARMBLOCK_ORIGIN;
	memmove(dst, src, PAGE_SIZE);
	*ipl_ptr = IPL_PARMBLOCK_ORIGIN;
}

static LIST_HEAD(rcall);
static DEFINE_MUTEX(rcall_mutex);

void register_reset_call(struct reset_call *reset)
{
	mutex_lock(&rcall_mutex);
	list_add(&reset->list, &rcall);
	mutex_unlock(&rcall_mutex);
}
EXPORT_SYMBOL_GPL(register_reset_call);

void unregister_reset_call(struct reset_call *reset)
{
	mutex_lock(&rcall_mutex);
	list_del(&reset->list);
	mutex_unlock(&rcall_mutex);
}
EXPORT_SYMBOL_GPL(unregister_reset_call);

static void do_reset_calls(void)
{
	struct reset_call *reset;

	list_for_each_entry(reset, &rcall, list)
		reset->fn();
}

u32 dump_prefix_page;

void s390_reset_system(void)
{
	struct _lowcore *lc;

	lc = (struct _lowcore *)(unsigned long) store_prefix();

	/* Stack for interrupt/machine check handler */
	lc->panic_stack = S390_lowcore.panic_stack;

	/* Save prefix page address for dump case */
	dump_prefix_page = (u32)(unsigned long) lc;

	/* Disable prefixing */
	set_prefix(0);

	/* Disable lowcore protection */
	__ctl_clear_bit(0,28);

	/* Set new machine check handler */
	S390_lowcore.mcck_new_psw.mask = psw_kernel_bits & ~PSW_MASK_MCHECK;
	S390_lowcore.mcck_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) s390_base_mcck_handler;

	/* Set new program check handler */
	S390_lowcore.program_new_psw.mask = psw_kernel_bits & ~PSW_MASK_MCHECK;
	S390_lowcore.program_new_psw.addr =
		PSW_ADDR_AMODE | (unsigned long) s390_base_pgm_handler;

	do_reset_calls();
}

