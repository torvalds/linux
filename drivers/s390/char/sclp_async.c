/*
 * Enable Asynchronous Notification via SCLP.
 *
 * Copyright IBM Corp. 2009
 * Author(s): Hans-Joachim Picht <hans@linux.vnet.ibm.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/utsname.h>
#include "sclp.h"

static int callhome_enabled;
static struct sclp_req *request;
static struct sclp_async_sccb *sccb;
static int sclp_async_send_wait(char *message);
static struct ctl_table_header *callhome_sysctl_header;
static DEFINE_SPINLOCK(sclp_async_lock);
#define SCLP_NORMAL_WRITE	0x00

struct async_evbuf {
	struct evbuf_header header;
	u64 reserved;
	u8 rflags;
	u8 empty;
	u8 rtype;
	u8 otype;
	char comp_id[12];
	char data[3000]; /* there is still some space left */
} __attribute__((packed));

struct sclp_async_sccb {
	struct sccb_header header;
	struct async_evbuf evbuf;
} __attribute__((packed));

static struct sclp_register sclp_async_register = {
	.send_mask = EVTYP_ASYNC_MASK,
};

static int call_home_on_panic(struct notifier_block *self,
			      unsigned long event, void *data)
{
	strncat(data, init_utsname()->nodename,
		sizeof(init_utsname()->nodename));
	sclp_async_send_wait(data);
	return NOTIFY_DONE;
}

static struct notifier_block call_home_panic_nb = {
	.notifier_call = call_home_on_panic,
	.priority = INT_MAX,
};

static int proc_handler_callhome(struct ctl_table *ctl, int write,
				 void __user *buffer, size_t *count,
				 loff_t *ppos)
{
	unsigned long val;
	int len, rc;
	char buf[3];

	if (!*count || (*ppos && !write)) {
		*count = 0;
		return 0;
	}
	if (!write) {
		len = snprintf(buf, sizeof(buf), "%d\n", callhome_enabled);
		rc = copy_to_user(buffer, buf, sizeof(buf));
		if (rc != 0)
			return -EFAULT;
	} else {
		len = *count;
		rc = copy_from_user(buf, buffer, sizeof(buf));
		if (rc != 0)
			return -EFAULT;
		if (strict_strtoul(buf, 0, &val) != 0)
			return -EINVAL;
		if (val != 0 && val != 1)
			return -EINVAL;
		callhome_enabled = val;
	}
	*count = len;
	*ppos += len;
	return 0;
}

static struct ctl_table callhome_table[] = {
	{
		.procname	= "callhome",
		.mode		= 0644,
		.proc_handler	= proc_handler_callhome,
	},
	{ .ctl_name = 0 }
};

static struct ctl_table kern_dir_table[] = {
	{
		.ctl_name	= CTL_KERN,
		.procname	= "kernel",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= callhome_table,
	},
	{ .ctl_name = 0 }
};

/*
 * Function used to transfer asynchronous notification
 * records which waits for send completion
 */
static int sclp_async_send_wait(char *message)
{
	struct async_evbuf *evb;
	int rc;
	unsigned long flags;

	if (!callhome_enabled)
		return 0;
	sccb->evbuf.header.type = EVTYP_ASYNC;
	sccb->evbuf.rtype = 0xA5;
	sccb->evbuf.otype = 0x00;
	evb = &sccb->evbuf;
	request->command = SCLP_CMDW_WRITE_EVENT_DATA;
	request->sccb = sccb;
	request->status = SCLP_REQ_FILLED;
	strncpy(sccb->evbuf.data, message, sizeof(sccb->evbuf.data));
	/*
	 * Retain Queue
	 * e.g. 5639CC140 500 Red Hat RHEL5 Linux for zSeries (RHEL AS)
	 */
	strncpy(sccb->evbuf.comp_id, "000000000", sizeof(sccb->evbuf.comp_id));
	sccb->evbuf.header.length = sizeof(sccb->evbuf);
	sccb->header.length = sizeof(sccb->evbuf) + sizeof(sccb->header);
	sccb->header.function_code = SCLP_NORMAL_WRITE;
	rc = sclp_add_request(request);
	if (rc)
		return rc;
	spin_lock_irqsave(&sclp_async_lock, flags);
	while (request->status != SCLP_REQ_DONE &&
		request->status != SCLP_REQ_FAILED) {
		 sclp_sync_wait();
	}
	spin_unlock_irqrestore(&sclp_async_lock, flags);
	if (request->status != SCLP_REQ_DONE)
		return -EIO;
	rc = ((struct sclp_async_sccb *)
	       request->sccb)->header.response_code;
	if (rc != 0x0020)
		return -EIO;
	if (evb->header.flags != 0x80)
		return -EIO;
	return rc;
}

static int __init sclp_async_init(void)
{
	int rc;

	rc = sclp_register(&sclp_async_register);
	if (rc)
		return rc;
	rc = -EOPNOTSUPP;
	if (!(sclp_async_register.sclp_receive_mask & EVTYP_ASYNC_MASK))
		goto out_sclp;
	rc = -ENOMEM;
	callhome_sysctl_header = register_sysctl_table(kern_dir_table);
	if (!callhome_sysctl_header)
		goto out_sclp;
	request = kzalloc(sizeof(struct sclp_req), GFP_KERNEL);
	sccb = (struct sclp_async_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!request || !sccb)
		goto out_mem;
	rc = atomic_notifier_chain_register(&panic_notifier_list,
					    &call_home_panic_nb);
	if (!rc)
		goto out;
out_mem:
	kfree(request);
	free_page((unsigned long) sccb);
	unregister_sysctl_table(callhome_sysctl_header);
out_sclp:
	sclp_unregister(&sclp_async_register);
out:
	return rc;
}
module_init(sclp_async_init);

static void __exit sclp_async_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &call_home_panic_nb);
	unregister_sysctl_table(callhome_sysctl_header);
	sclp_unregister(&sclp_async_register);
	free_page((unsigned long) sccb);
	kfree(request);
}
module_exit(sclp_async_exit);

MODULE_AUTHOR("Copyright IBM Corp. 2009");
MODULE_AUTHOR("Hans-Joachim Picht <hans@linux.vnet.ibm.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SCLP Asynchronous Notification Records");
