/*
 *  drivers/s390/char/sclp_cpi_sys.c
 *    SCLP control program identification sysfs interface
 *
 *    Copyright IBM Corp. 2001, 2007
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Michael Ernst <mernst@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <asm/ebcdic.h>
#include <asm/sclp.h>
#include "sclp.h"
#include "sclp_rw.h"
#include "sclp_cpi_sys.h"

#define CPI_LENGTH_NAME 8
#define CPI_LENGTH_LEVEL 16

struct cpi_evbuf {
	struct evbuf_header header;
	u8	id_format;
	u8	reserved0;
	u8	system_type[CPI_LENGTH_NAME];
	u64	reserved1;
	u8	system_name[CPI_LENGTH_NAME];
	u64	reserved2;
	u64	system_level;
	u64	reserved3;
	u8	sysplex_name[CPI_LENGTH_NAME];
	u8	reserved4[16];
} __attribute__((packed));

struct cpi_sccb {
	struct sccb_header header;
	struct cpi_evbuf cpi_evbuf;
} __attribute__((packed));

static struct sclp_register sclp_cpi_event = {
	.send_mask = EVTYP_CTLPROGIDENT_MASK,
};

static char system_name[CPI_LENGTH_NAME + 1];
static char sysplex_name[CPI_LENGTH_NAME + 1];
static char system_type[CPI_LENGTH_NAME + 1];
static u64 system_level;

static void set_data(char *field, char *data)
{
	memset(field, ' ', CPI_LENGTH_NAME);
	memcpy(field, data, strlen(data));
	sclp_ascebc_str(field, CPI_LENGTH_NAME);
}

static void cpi_callback(struct sclp_req *req, void *data)
{
	struct completion *completion = data;

	complete(completion);
}

static struct sclp_req *cpi_prepare_req(void)
{
	struct sclp_req *req;
	struct cpi_sccb *sccb;
	struct cpi_evbuf *evb;

	req = kzalloc(sizeof(struct sclp_req), GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);
	sccb = (struct cpi_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb) {
		kfree(req);
		return ERR_PTR(-ENOMEM);
	}

	/* setup SCCB for Control-Program Identification */
	sccb->header.length = sizeof(struct cpi_sccb);
	sccb->cpi_evbuf.header.length = sizeof(struct cpi_evbuf);
	sccb->cpi_evbuf.header.type = 0x0b;
	evb = &sccb->cpi_evbuf;

	/* set system type */
	set_data(evb->system_type, system_type);

	/* set system name */
	set_data(evb->system_name, system_name);

	/* set sytem level */
	evb->system_level = system_level;

	/* set sysplex name */
	set_data(evb->sysplex_name, sysplex_name);

	/* prepare request data structure presented to SCLP driver */
	req->command = SCLP_CMDW_WRITE_EVENT_DATA;
	req->sccb = sccb;
	req->status = SCLP_REQ_FILLED;
	req->callback = cpi_callback;
	return req;
}

static void cpi_free_req(struct sclp_req *req)
{
	free_page((unsigned long) req->sccb);
	kfree(req);
}

static int cpi_req(void)
{
	struct completion completion;
	struct sclp_req *req;
	int rc;
	int response;

	rc = sclp_register(&sclp_cpi_event);
	if (rc) {
		printk(KERN_WARNING "cpi: could not register "
			"to hardware console.\n");
		goto out;
	}
	if (!(sclp_cpi_event.sclp_receive_mask & EVTYP_CTLPROGIDENT_MASK)) {
		printk(KERN_WARNING "cpi: no control program "
			"identification support\n");
		rc = -EOPNOTSUPP;
		goto out_unregister;
	}

	req = cpi_prepare_req();
	if (IS_ERR(req)) {
		printk(KERN_WARNING "cpi: could not allocate request\n");
		rc = PTR_ERR(req);
		goto out_unregister;
	}

	init_completion(&completion);
	req->callback_data = &completion;

	/* Add request to sclp queue */
	rc = sclp_add_request(req);
	if (rc) {
		printk(KERN_WARNING "cpi: could not start request\n");
		goto out_free_req;
	}

	wait_for_completion(&completion);

	if (req->status != SCLP_REQ_DONE) {
		printk(KERN_WARNING "cpi: request failed (status=0x%02x)\n",
			req->status);
		rc = -EIO;
		goto out_free_req;
	}

	response = ((struct cpi_sccb *) req->sccb)->header.response_code;
	if (response != 0x0020) {
		printk(KERN_WARNING "cpi: failed with "
			"response code 0x%x\n", response);
		rc = -EIO;
	}

out_free_req:
	cpi_free_req(req);

out_unregister:
	sclp_unregister(&sclp_cpi_event);

out:
	return rc;
}

static int check_string(const char *attr, const char *str)
{
	size_t len;
	size_t i;

	len = strlen(str);

	if ((len > 0) && (str[len - 1] == '\n'))
		len--;

	if (len > CPI_LENGTH_NAME)
		return -EINVAL;

	for (i = 0; i < len ; i++) {
		if (isalpha(str[i]) || isdigit(str[i]) ||
		    strchr("$@# ", str[i]))
			continue;
		return -EINVAL;
	}

	return 0;
}

static void set_string(char *attr, const char *value)
{
	size_t len;
	size_t i;

	len = strlen(value);

	if ((len > 0) && (value[len - 1] == '\n'))
		len--;

	for (i = 0; i < CPI_LENGTH_NAME; i++) {
		if (i < len)
			attr[i] = toupper(value[i]);
		else
			attr[i] = ' ';
	}
}

static ssize_t system_name_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", system_name);
}

static ssize_t system_name_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
	size_t len)
{
	int rc;

	rc = check_string("system_name", buf);
	if (rc)
		return rc;

	set_string(system_name, buf);

	return len;
}

static struct kobj_attribute system_name_attr =
	__ATTR(system_name, 0644, system_name_show, system_name_store);

static ssize_t sysplex_name_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", sysplex_name);
}

static ssize_t sysplex_name_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf,
	size_t len)
{
	int rc;

	rc = check_string("sysplex_name", buf);
	if (rc)
		return rc;

	set_string(sysplex_name, buf);

	return len;
}

static struct kobj_attribute sysplex_name_attr =
	__ATTR(sysplex_name, 0644, sysplex_name_show, sysplex_name_store);

static ssize_t system_type_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", system_type);
}

static ssize_t system_type_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
	size_t len)
{
	int rc;

	rc = check_string("system_type", buf);
	if (rc)
		return rc;

	set_string(system_type, buf);

	return len;
}

static struct kobj_attribute system_type_attr =
	__ATTR(system_type, 0644, system_type_show, system_type_store);

static ssize_t system_level_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *page)
{
	unsigned long long level = system_level;

	return snprintf(page, PAGE_SIZE, "%#018llx\n", level);
}

static ssize_t system_level_store(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  const char *buf,
	size_t len)
{
	unsigned long long level;
	char *endp;

	level = simple_strtoull(buf, &endp, 16);

	if (endp == buf)
		return -EINVAL;
	if (*endp == '\n')
		endp++;
	if (*endp)
		return -EINVAL;

	system_level = level;

	return len;
}

static struct kobj_attribute system_level_attr =
	__ATTR(system_level, 0644, system_level_show, system_level_store);

static ssize_t set_store(struct kobject *kobj,
			 struct kobj_attribute *attr,
			 const char *buf, size_t len)
{
	int rc;

	rc = cpi_req();
	if (rc)
		return rc;

	return len;
}

static struct kobj_attribute set_attr = __ATTR(set, 0200, NULL, set_store);

static struct attribute *cpi_attrs[] = {
	&system_name_attr.attr,
	&sysplex_name_attr.attr,
	&system_type_attr.attr,
	&system_level_attr.attr,
	&set_attr.attr,
	NULL,
};

static struct attribute_group cpi_attr_group = {
	.attrs = cpi_attrs,
};

static struct kset *cpi_kset;

int sclp_cpi_set_data(const char *system, const char *sysplex, const char *type,
		      const u64 level)
{
	int rc;

	rc = check_string("system_name", system);
	if (rc)
		return rc;
	rc = check_string("sysplex_name", sysplex);
	if (rc)
		return rc;
	rc = check_string("system_type", type);
	if (rc)
		return rc;

	set_string(system_name, system);
	set_string(sysplex_name, sysplex);
	set_string(system_type, type);
	system_level = level;

	return cpi_req();
}
EXPORT_SYMBOL(sclp_cpi_set_data);

static int __init cpi_init(void)
{
	int rc;

	cpi_kset = kset_create_and_add("cpi", NULL, firmware_kobj);
	if (!cpi_kset)
		return -ENOMEM;

	rc = sysfs_create_group(&cpi_kset->kobj, &cpi_attr_group);
	if (rc)
		kset_unregister(cpi_kset);

	return rc;
}

__initcall(cpi_init);
