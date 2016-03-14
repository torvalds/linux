/*
 * IBM eServer i/pSeries Virtual SCSI Target Driver
 * Copyright (C) 2003-2005 Dave Boutcher (boutcher@us.ibm.com) IBM Corp.
 *			   Santiago Leon (santil@us.ibm.com) IBM Corp.
 *			   Linda Xie (lxie@us.ibm.com) IBM Corp.
 *
 * Copyright (C) 2005-2011 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2010 Nicholas A. Bellinger <nab@kernel.org>
 * Copyright (C) 2016 Bryant G. Ly <bgly@us.ibm.com> IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/libsrp.h>
#include <generated/utsrelease.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_backend.h>

#include <asm/hvcall.h>
#include <asm/iommu.h>
#include <asm/prom.h>
#include <asm/vio.h>

#include "ibmvscsi.h"
#include "viosrp.h"

#define IBMVSCSIS_VERSION	"v0.1"
#define IBMVSCSIS_NAMELEN	32

#define	INITIAL_SRP_LIMIT	16
#define	DEFAULT_MAX_SECTORS	256

#define MAX_H_COPY_RDMA		(128*1024)

/*
 * Hypervisor calls.
 */
#define h_send_crq(ua, l, h) \
			plpar_hcall_norets(H_SEND_CRQ, ua, l, h)
#define h_reg_crq(ua, tok, sz)\
			plpar_hcall_norets(H_REG_CRQ, ua, tok, sz);

#define GETTARGET(x) ((int)((((u64)(x)) >> 56) & 0x003f))
#define GETBUS(x) ((int)((((u64)(x)) >> 53) & 0x0007))
#define GETLUN(x) ((int)((((u64)(x)) >> 48) & 0x001f))

#define BUILD_BUG_ON_NOT_POWER_OF_2(n)		\
	BUILD_BUG_ON((n) == 0 || (((n) & ((n) - 1)) != 0))

/*
 * These are fixed for the system and come from the Open Firmware device tree.
 * We just store them here to save getting them every time.
 */

static unsigned max_vdma_size = MAX_H_COPY_RDMA;

static const char ibmvscsis_driver_name[] = "ibmvscsis";
static const char ibmvscsis_workq_name[] = "ibmvscsis";
static char system_id[64] = "";
static char partition_name[97] = "UNKNOWN";
static unsigned int partition_number = -1;

static DEFINE_MUTEX(tpg_mutex);
static LIST_HEAD(tpg_list);
static DEFINE_SPINLOCK(ibmvscsis_dev_lock);
static LIST_HEAD(ibmvscsis_dev_list);

struct ibmvscsis_cmnd {
	/* Used for libsrp processing callbacks */
	struct scsi_cmnd sc;
	/* Used for TCM Core operations */
	struct se_cmd se_cmd;
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char sense_buf[TRANSPORT_SENSE_BUFFER];
	/* Pointer to ibmvscsis nexus memory */
	struct ibmvscsis_nexus *ibmvscsis_nexus;
};

struct ibmvscsis_crq_msg {
	u8 valid;
	u8 format;
	u8 rsvd;
	u8 status;
	u16 rsvd1;
	__be16 IU_length;
	__be64 IU_data_ptr;
};

struct ibmvscsis_nexus {
	/* Pointer to TCM session for I_T Nexus */
	struct se_session *se_sess;
};

struct ibmvscsis_tpg {
	/* ibmvscsis port target portal group tag for TCM */
	u16 tport_tpgt;
	/* Used to track number of TPG Port/Lun Links wrt to explict
	 * I_T Nexus shutdown
	 */
	int tpg_port_count;
	/* Used for ibmvscsis device reference to tpg_nexus, protected
	 * by tpg_mutex
	 */
	int tpg_ibmvscsis_count;
	/* list for ibmvscsis_list */
	struct list_head tpg_list;
	/* Used to protect access for tpg_nexus */
	struct mutex tpg_mutex;
	/* Pointer to the TCM ibmvscsis I_T Nexus for this TPG endpoint */
	struct ibmvscsis_nexus *tpg_nexus;
	/* Pointer back to ibmvscsis_tport */
	struct ibmvscsis_tport *tport;
	/* Returned by ibmvscsis_make_tpg() */
	struct se_portal_group se_tpg;
	/* Pointer back to ibmvscsis, protected by tpg_mutex */
	struct ibmvscsis_adapter *ibmvscsis_adapter;
};

struct ibmvscsis_tport {
	/* SCSI protocol the tport is providing */
	u8 tport_proto_id;
	/* Binary World Wide unique Port Name for SRP Target port */
	u64 tport_wwpn;
	/* ASCII formatted WWPN for SRP Target port */
	char tport_name[IBMVSCSIS_NAMELEN];
	/* Returned by ibmvscsis_make_tport() */
	struct se_wwn tport_wwn;
};

struct ibmvscsis_nacl {
	/* Returned by ibmvscsis_make_nodeacl() */
	struct se_node_acl se_node_acl;
	/* Binary World Wide unique Port Name for SRP Initiator port */
	u64 iport_wwpn;
	/* ASCII formatted WWPN for Sas Initiator port */
	char iport_name[IBMVSCSIS_NAMELEN];
};

struct ibmvscsis_adapter {
	struct ibmvscsis_tpg *tpg;

	struct device dev;
	struct vio_dev *dma_dev;
	struct list_head siblings;

	struct crq_queue crq_queue;
	struct tasklet_struct work_task;

	u32 liobn;
	u32 riobn;

	//TODO: FIX LIBSRP TO WORK WITH TCM
	struct srp_target *target;

	bool release;
	struct list_head list;
	struct ibmvscsis_tport *tport;
};

static inline long h_copy_rdma(s64 length, u64 sliobn, u64 slioba,
	u64 dliobn, u64 dlioba)
{
	long rc = 0;

	rc = plpar_hcall_norets(H_COPY_RDMA, length, sliobn, slioba,
			dliobn, dlioba);
	return rc;
}

static inline void h_free_crq(uint32_t unit_address)
{
	long rc = 0;

	do {
		if (H_IS_LONG_BUSY(rc))
			msleep(get_longbusy_msecs(rc));

		rc = plpar_hcall_norets(H_FREE_CRQ, unit_address);
	} while ((rc == H_BUSY) || (H_IS_LONG_BUSY(rc)));
}

static int ibmvscsis_check_true(struct se_portal_group *se_tpg)
{
	return 1;
}

static int ibmvscsis_check_false(struct se_portal_group *se_tpg)
{
	return 0;
}

static char *ibmvscsis_get_fabric_name(void)
{
	return "ibmvscsis";
}

static char *ibmvscsis_get_fabric_wwn(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tpg *tpg =
		container_of(se_tpg, struct ibmvscsis_tpg, se_tpg);
	struct ibmvscsis_tport *tport = tpg->tport;

	return &tport->tport_name[0];
}

static u16 ibmvscsis_get_tag(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tpg *tpg =
		container_of(se_tpg, struct ibmvscsis_tpg, se_tpg);

	return tpg->tport_tpgt;
}

static u32 ibmvscsis_get_default_depth(struct se_portal_group *se_tpg)
{
	return 1;
}

static u32 ibmvscsis_tpg_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static void ibmvscsis_release_cmd(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd =
		container_of(se_cmd, struct ibmvscsis_cmnd, se_cmd);

	kfree(cmd);
}

static int ibmvscsis_shutdown_session(struct se_session *se_sess)
{
	return 0;
}

static void ibmvscsis_close_session(struct se_session *se_sess)
{
	return;
}

static u32 ibmvscsis_sess_get_index(struct se_session *se_sess)
{
	return 0;
}

static int ibmvscsis_write_pending_status(struct se_cmd *se_cmd)
{
	return 0;
}

static void ibmvscsis_set_default_node_attrs(struct se_node_acl *nacl)
{
	return;
}

static int ibmvscsis_get_cmd_state(struct se_cmd *se_cmd)
{
	return 0;
}

static void ibmvscsis_queue_tm_rsp(struct se_cmd *se_cmd)
{
	return;
}

static ssize_t ibmvscsis_wwn_version_show(struct config_item *item,
					       char *page)
{
	return sprintf(page, "IBMVSCSIS fabric module %s on %s/%s"
		"on "UTS_RELEASE"\n", IBMVSCSIS_VERSION, utsname()->sysname,
		utsname()->machine);
}

CONFIGFS_ATTR_RO(ibmvscsis_wwn_, version);

static struct configfs_attribute *ibmvscsis_wwn_attrs[] = {
	&ibmvscsis_wwn_attr_version,
	NULL,
};

static int ibmvscsis_write_pending(struct se_cmd *se_cmd);
static int ibmvscsis_queue_data_in(struct se_cmd *se_cmd);
static int ibmvscsis_queue_status(struct se_cmd *se_cmd);
static int ibmvscsis_check_stop_free(struct se_cmd *se_cmd)
{
	return target_put_sess_cmd(se_cmd);
}
static void ibmvscsis_aborted_task(struct se_cmd *se_cmd)
{
	pr_debug("ibmvscsis: ibmvscsis_aborted_task\n");
	return;
}

static int ibmvscsis_make_nexus(struct ibmvscsis_tpg *tpg,
				const char *name)
{
	struct se_portal_group *se_tpg;
	struct ibmvscsis_nexus *nexus;

	pr_debug("ibmvscsis: make nexus");
	mutex_lock(&tpg->tpg_mutex);
	if (tpg->tpg_nexus) {
		mutex_unlock(&tpg->tpg_mutex);
		pr_debug("tpg->tpg_nexus already exists\n");
		return -EEXIST;
	}
	se_tpg = &tpg->se_tpg;

	nexus = kzalloc(sizeof(struct ibmvscsis_nexus), GFP_KERNEL);
	if (!nexus) {
		mutex_unlock(&tpg->tpg_mutex);
		pr_err("Unable to allocate struct ibmvscsis_nexus\n");
		return -ENOMEM;
	}
	/*
	 *  Initialize the struct se_session pointer and setup tagpool
	 *  for struct ibmvscsis_cmd descriptors
	 */
	nexus->se_sess = transport_init_session(TARGET_PROT_NORMAL);
	if (IS_ERR(nexus->se_sess)) {
		mutex_unlock(&tpg->tpg_mutex);
		goto transport_init_fail;
	}
	/*
	 * Since we are running in 'demo mode' this call will generate a
	 * struct se_node_acl for the ibmvscsis struct se_portal_group with
	 * the SCSI Initiator port name of the passed configfs group 'name'.
	 */
	nexus->se_sess->se_node_acl = core_tpg_check_initiator_node_acl(
				se_tpg, (unsigned char *)name);
	if (!nexus->se_sess->se_node_acl) {
		mutex_unlock(&tpg->tpg_mutex);
		pr_debug("core_tpg_check_initiator_node_acl() failed"
				" for %s\n", name);
		goto acl_failed;
	}
	/*
	 * Now register the TCM ibmvscsis virtual I_T Nexus as active.
	 */
	transport_register_session(se_tpg, nexus->se_sess->se_node_acl,
			nexus->se_sess, nexus);
	tpg->tpg_nexus = nexus;

	mutex_unlock(&tpg->tpg_mutex);
	return 0;

acl_failed:
	transport_free_session(nexus->se_sess);
transport_init_fail:
	kfree(nexus);
	return -ENOMEM;
}

static int ibmvscsis_drop_nexus(struct ibmvscsis_tpg *tpg)
{
	struct se_session *se_sess;
	struct ibmvscsis_nexus *nexus;

	pr_debug("ibmvscsis: drop nexus");
	mutex_lock(&tpg->tpg_mutex);
	nexus = tpg->tpg_nexus;
	if (!nexus) {
		mutex_unlock(&tpg->tpg_mutex);
		return -ENODEV;
	}

	se_sess = nexus->se_sess;
	if (!se_sess) {
		mutex_unlock(&tpg->tpg_mutex);
		return -ENODEV;
	}

	if (tpg->tpg_port_count != 0) {
		mutex_unlock(&tpg->tpg_mutex);
		pr_err("Unable to remove TCM_ibmvscsis I_T Nexus with"
			" active TPG port count: %d\n",
			tpg->tpg_port_count);
		return -EBUSY;
	}

	if (tpg->tpg_ibmvscsis_count != 0) {
		mutex_unlock(&tpg->tpg_mutex);
		pr_err("Unable to remove TCM_ibmvscsis I_T Nexus with"
			" active TPG ibmvscsis count: %d\n",
			tpg->tpg_ibmvscsis_count);
		return -EBUSY;
	}

	/*
	 * Release the SCSI I_T Nexus to the emulated ibmvscsis Target Port
	 */
	transport_deregister_session(nexus->se_sess);
	tpg->tpg_nexus = NULL;
	mutex_unlock(&tpg->tpg_mutex);

	kfree(nexus);
	return 0;
}

static ssize_t ibmvscsis_tpg_nexus_show(struct config_item *item, char *page)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct ibmvscsis_tpg *tpg = container_of(se_tpg,
				struct ibmvscsis_tpg, se_tpg);
	struct ibmvscsis_nexus *nexus;
	ssize_t ret;

	pr_debug("ibmvscsis: tpg nexus show");
	mutex_lock(&tpg->tpg_mutex);
	nexus = tpg->tpg_nexus;
	if (!nexus) {
		mutex_unlock(&tpg->tpg_mutex);
		return -ENODEV;
	}
	ret = snprintf(page, PAGE_SIZE, "%s\n",
			nexus->se_sess->se_node_acl->initiatorname);
	mutex_unlock(&tpg->tpg_mutex);

	return ret;
}

static ssize_t ibmvscsis_tpg_nexus_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_portal_group *se_tpg = to_tpg(item);
	struct ibmvscsis_tpg *tpg = container_of(se_tpg,
				struct ibmvscsis_tpg, se_tpg);
	struct ibmvscsis_tport *tport_wwn = tpg->tport;
	unsigned char i_port[256], *ptr, *port_ptr;
	int ret;
	pr_debug("ibmvscsis: tpg nexus store");
	/*
	 * Shutdown the active I_T nexus if 'NULL' is passed..
	 */
	if (!strncmp(page, "NULL", 4)) {
		ret = ibmvscsis_drop_nexus(tpg);
		return (!ret) ? count : ret;
	}
	/*
	 * Otherwise make sure the passed virtual Initiator port WWN matches
	 * the fabric protocol_id set in ibmvscsis_make_tport(), and call
	 * ibmvscsis_make_nexus().
	 */
	if (strlen(page) >= 256) {
		pr_err("Emulated NAA Sas Address: %s, exceeds"
				" max: %d\n", page, 256);
		return -EINVAL;
	}
	snprintf(&i_port[0], 256, "%s", page);

	ptr = strstr(i_port, "naa.");
	if (ptr) {
		if (tport_wwn->tport_proto_id != SCSI_PROTOCOL_SRP) {
			pr_err("Passed SRP Initiator Port %s does not"
				" match target port protoid: \n", i_port);
			return -EINVAL;
		}
		port_ptr = &i_port[0];
		goto check_newline;
	}
	pr_err("Unable to locate prefix for emulated Initiator Port:"
			" %s\n", i_port);
	return -EINVAL;
	/*
	 * Clear any trailing newline for the NAA WWN
	 */
check_newline:
	if (i_port[strlen(i_port)-1] == '\n')
		i_port[strlen(i_port)-1] = '\0';

	/*
	 * Called creation of nexus here as a hack since actual use of nexus
	 * isn't working with targetcli/config files.
	 */
	ret = ibmvscsis_make_nexus(tpg, port_ptr);
	if (ret < 0)
		return ret;

	return count;
}

CONFIGFS_ATTR(ibmvscsis_tpg_, nexus);

static struct configfs_attribute *ibmvscsis_tpg_attrs[] = {
	&ibmvscsis_tpg_attr_nexus,
	NULL,
};

static void ibmvscsis_drop_tpg(struct se_portal_group *se_tpg)
{
	struct ibmvscsis_tpg *tpg = container_of(se_tpg,
				struct ibmvscsis_tpg, se_tpg);

	//TODO: Add a release mechanism to remove vio
	mutex_lock(&tpg_mutex);
	list_del(&tpg->tpg_list);
	mutex_unlock(&tpg_mutex);
	/*
	 * Release the virtual I_T Nexus for this ibmvscsis TPG
	 */
	ibmvscsis_drop_nexus(tpg);
	/*
	 * Deregister the se_tpg from TCM..
	 */
	core_tpg_deregister(se_tpg);
	kfree(tpg);
}

static struct se_portal_group *ibmvscsis_make_tpg(struct se_wwn *wwn,
						  struct config_group *group,
						  const char *name)
{
	struct ibmvscsis_tport *tport =
		container_of(wwn, struct ibmvscsis_tport, tport_wwn);
	struct ibmvscsis_tpg *tpg;
	u16 tpgt;
	int ret;

	if (strstr(name, "tpgt_") != name)
		return ERR_PTR(-EINVAL);
	if (kstrtou16(name + 5, 10, &tpgt) || tpgt >= DEFAULT_MAX_SECTORS)
		return ERR_PTR(-EINVAL);

	tpg = kzalloc(sizeof(struct ibmvscsis_tpg), GFP_KERNEL);
	if (!tpg) {
		pr_err("Unable to allocate struct ibmvscsis_tpg");
		return ERR_PTR(-ENOMEM);
	}
	mutex_init(&tpg->tpg_mutex);
	INIT_LIST_HEAD(&tpg->tpg_list);
	tpg->tport = tport;
	tpg->tport_tpgt = tpgt;

	pr_debug("ibmvscsis: make_tpg name:%s, tport_proto_id:%x\n",
			name, tport->tport_proto_id);

	ret = core_tpg_register(wwn, &tpg->se_tpg, tport->tport_proto_id);
	if (ret < 0) {
		kfree(tpg);
		return NULL;
	}
	mutex_lock(&tpg_mutex);
	list_add_tail(&tpg->tpg_list, &tpg_list);
	mutex_unlock(&tpg_mutex);

	if(ibmvscsis_make_nexus(tpg, name) < 0) {
		pr_info("ibmvscsis: failed make nexus\n");
		ibmvscsis_drop_tpg(&tpg->se_tpg);
	}

	return &tpg->se_tpg;
}

static struct ibmvscsis_tport *ibmvscsis_lookup_port(const char *name)
{
	struct ibmvscsis_tport *tport;
	struct vio_dev *vdev;
	struct ibmvscsis_adapter *adapter;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&ibmvscsis_dev_lock, flags);
	list_for_each_entry(adapter, &ibmvscsis_dev_list, list) {
		vdev = adapter->dma_dev;
		pr_debug("ibmvscsis: lookup adapter ptr: %p\n", adapter);
		pr_debug("ibmvscsis:lookup_port ptr:%p\n", vdev);
		ret = strcmp(dev_name(&vdev->dev), name);
		if(ret == 0) {
			pr_debug("ibmvscsis: lookup ret: %x, :port%p\n",
					ret, adapter->tport);
			tport = adapter->tport;
		}
		if(tport)
			goto found;
	}
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);
	return NULL;
found:
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);
	return tport;
}

static struct se_wwn *ibmvscsis_make_tport(struct target_fabric_configfs *tf,
					   struct config_group *group,
					   const char *name)
{
	struct ibmvscsis_tport *tport;
	char *ptr;
	u64 wwpn = 0;

	tport = ibmvscsis_lookup_port(name);
	pr_debug("make_tport(%s), pointer:%p\n", name, tport);
	if(!tport)
		return NULL;

	tport->tport_wwpn = wwpn;

	pr_debug("ibmvscsis: make_tport name:%s, %x\n", name,
					tport->tport_proto_id);
	ptr = strstr(name, "naa.");
	if(ptr) {
		tport->tport_proto_id = SCSI_PROTOCOL_SRP;
		goto check_len;
	}
	//TODO: Fix to use something better than 300
	ptr = strstr(name, "300");
	if(ptr) {
		tport->tport_proto_id = SCSI_PROTOCOL_SRP;
		goto check_len;
	}
check_len:
	snprintf(&tport->tport_name[0], 256, "%s", name);
	return &tport->tport_wwn;
}

static void ibmvscsis_drop_tport(struct se_wwn *wwn)
{
	struct ibmvscsis_tport *tport = container_of(wwn,
				struct ibmvscsis_tport, tport_wwn);

	kfree(tport);
}

static ssize_t system_id_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
        return snprintf(buf, PAGE_SIZE, "%s\n", system_id);
}

static ssize_t partition_number_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
        return snprintf(buf, PAGE_SIZE, "%x\n", partition_number);
}

static ssize_t unit_address_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
{
        struct ibmvscsis_adapter *adapter =
                container_of(dev, struct ibmvscsis_adapter, dev);
        return snprintf(buf, PAGE_SIZE, "%x\n", adapter->dma_dev->unit_address);
}

static struct class_attribute ibmvscsis_class_attrs[] = {
        __ATTR_NULL,
};

static struct device_attribute dev_attr_system_id =
        __ATTR(system_id, S_IRUGO, system_id_show, NULL);

static struct device_attribute dev_attr_partition_number =
        __ATTR(partition_number, S_IRUGO, partition_number_show, NULL);

static struct device_attribute dev_attr_unit_address =
        __ATTR(unit_address, S_IRUGO, unit_address_show, NULL);

static struct attribute *ibmvscsis_dev_attrs[] = {
        &dev_attr_system_id.attr,
        &dev_attr_partition_number.attr,
        &dev_attr_unit_address.attr,
        NULL
};
ATTRIBUTE_GROUPS(ibmvscsis_dev);

static void ibmvscsis_dev_release(struct device *dev) {};

static struct class ibmvscsis_class = {
        .name           = "ibmvscsis",
        .dev_release    = ibmvscsis_dev_release,
        .class_attrs    = ibmvscsis_class_attrs,
        .dev_groups     = ibmvscsis_dev_groups,
};

static inline union viosrp_iu *vio_iu(struct iu_entry *iue)
{
	return (union viosrp_iu *)(iue->sbuf->buf);
}

static int send_iu(struct iu_entry *iue, u64 length, u8 format)
{
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	struct ibmvscsis_crq_msg crq_msg;
	__be64 *crq_as_u64 = (__be64*)&crq_msg;
	long rc, rc1;

	pr_debug("ibmvscsis: send_iu: 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx\n",
			(unsigned long)length,
			(unsigned long)adapter->liobn,
			(unsigned long)iue->sbuf->dma,
			(unsigned long)adapter->riobn,
			(unsigned long)be64_to_cpu(iue->remote_token));

	/* First copy the SRP */
	rc = h_copy_rdma(length, adapter->liobn, iue->sbuf->dma,
			 adapter->riobn, be64_to_cpu(iue->remote_token));

	if (rc)
		pr_err("ibmvscsis: Error %ld transferring data\n", rc);

	pr_debug("ibmvscsis: crq pre cooked: 0x%x, 0x%llx, 0x%llx\n",
			format, length, vio_iu(iue)->srp.rsp.tag);

	crq_msg.valid = 0x80;
	crq_msg.format = format;
	crq_msg.rsvd = 0;
	if (rc == 0) {
		crq_msg.status = 0x99;
	} else {
		crq_msg.status = 0;
	}
	crq_msg.rsvd1 = 0;
	crq_msg.IU_length = cpu_to_be16(length);
	crq_msg.IU_data_ptr = vio_iu(iue)->srp.rsp.tag;

	pr_debug("ibmvscsis: send crq: 0x%x, 0x%llx, 0x%llx\n",
			adapter->dma_dev->unit_address,
			be64_to_cpu(crq_as_u64[0]),
			be64_to_cpu(crq_as_u64[1]));

//	srp_iu_put(iue);

	rc1 = h_send_crq(adapter->dma_dev->unit_address,
				be64_to_cpu(crq_as_u64[0]),
				be64_to_cpu(crq_as_u64[1]));

	if (rc1) {
		pr_err("ibmvscsis: %ld sending response\n", rc1);
		return rc1;
	}

	return rc;
}

#define SRP_RSP_SENSE_DATA_LEN	18

static int send_rsp(struct iu_entry *iue, struct scsi_cmnd *sc,
		    unsigned char status, unsigned char asc)
{
	union viosrp_iu *iu = vio_iu(iue);
	uint64_t tag = iu->srp.rsp.tag;

	/* If the linked bit is on and status is good */
	if (test_bit(V_LINKED, &iue->flags) && (status == NO_SENSE))
		status = 0x10;

	memset(iu, 0, sizeof(struct srp_rsp));
	iu->srp.rsp.opcode = SRP_RSP;
	iu->srp.rsp.req_lim_delta = 1;
	iu->srp.rsp.tag = tag;

	if (test_bit(V_DIOVER, &iue->flags))
		iu->srp.rsp.flags |= SRP_RSP_FLAG_DIOVER;

	iu->srp.rsp.data_in_res_cnt = 0;
	iu->srp.rsp.data_out_res_cnt = 0;

	iu->srp.rsp.flags &= ~SRP_RSP_FLAG_RSPVALID;

	iu->srp.rsp.resp_data_len = 0;
	iu->srp.rsp.status = status;
	if (status) {
		uint8_t *sense = iu->srp.rsp.data;

		if (sc) {
			iu->srp.rsp.flags |= SRP_RSP_FLAG_SNSVALID;
			iu->srp.rsp.sense_data_len = SCSI_SENSE_BUFFERSIZE;
			memcpy(sense, sc->sense_buffer, SCSI_SENSE_BUFFERSIZE);
		} else {
			iu->srp.rsp.status = SAM_STAT_CHECK_CONDITION;
			iu->srp.rsp.flags |= SRP_RSP_FLAG_SNSVALID;
			iu->srp.rsp.sense_data_len = SRP_RSP_SENSE_DATA_LEN;

			/* Valid bit and 'current errors' */
			sense[0] = (0x1 << 7 | 0x70);
			/* Sense key */
			sense[2] = status;
			/* Additional sense length */
			sense[7] = 0xa;	/* 10 bytes */
			/* Additional sense code */
			sense[12] = asc;
		}
	}

	send_iu(iue, sizeof(iu->srp.rsp) + SRP_RSP_SENSE_DATA_LEN,
		VIOSRP_SRP_FORMAT);

	return 0;
}

static int send_adapter_info(struct iu_entry *iue,
			     dma_addr_t remote_buffer, u16 length)
{
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	dma_addr_t data_token;
	struct mad_adapter_info_data *info;
	int err;

	info = dma_alloc_coherent(&adapter->dma_dev->dev, sizeof(*info),
				  &data_token, GFP_KERNEL);
	if (!info) {
		pr_err("ibmvscsis: bad dma_alloc_coherent %p\n", target);
		return 1;
	}

	pr_debug("ibmvscsis: get_remote_info: 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx\n",
			(unsigned long)sizeof(*info),
			(unsigned long)adapter->liobn,
			(unsigned long)data_token,
			(unsigned long)adapter->riobn,
			(unsigned long)remote_buffer);

	/* Get remote info */
	err = h_copy_rdma(sizeof(*info), adapter->riobn,
				be64_to_cpu(remote_buffer),
				adapter->liobn, data_token);

	if (err == H_SUCCESS) {
		pr_err("ibmvscsis: Client connect: %s (%d)\n",
		       info->partition_name, info->partition_number);
	}

	memset(info, 0, sizeof(*info));

	strcpy(info->srp_version, "16.a");
	strncpy(info->partition_name, partition_name,
		sizeof(info->partition_name));
	info->partition_number = partition_number;
	info->mad_version = 1;
	info->os_type = 2;
	info->port_max_txu[0] = DEFAULT_MAX_SECTORS << 9;

	pr_debug("ibmvscsis: send info to remote: 0x%lx 0x%lx 0x%lx \
			0x%lx 0x%lx\n",(unsigned long)sizeof(*info),
			(unsigned long)adapter->liobn,
			(unsigned long)data_token,
			(unsigned long)adapter->riobn,
			(unsigned long)remote_buffer);

	/* Send our info to remote */
	err = h_copy_rdma(sizeof(*info), adapter->liobn, data_token,
			  adapter->riobn, be64_to_cpu(remote_buffer));

	dma_free_coherent(&adapter->dma_dev->dev, sizeof(*info), info,
			  data_token);
	if (err != H_SUCCESS) {
		pr_err("ibmvscsis: Error sending adapter info %d\n", err);
		return 1;
	}

	return 0;
}

static int process_mad_iu(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct viosrp_adapter_info *info;
	struct viosrp_host_config *conf;

	switch (be32_to_cpu(iu->mad.empty_iu.common.type)) {
	case VIOSRP_EMPTY_IU_TYPE:
		pr_err("ibmvscsis: %s\n", "Unsupported EMPTY MAD IU");
		break;
	case VIOSRP_ERROR_LOG_TYPE:
		pr_err("ibmvscsis: %s\n", "Unsupported ERROR LOG MAD IU");
		iu->mad.error_log.common.status = 1;
		send_iu(iue, sizeof(iu->mad.error_log),	VIOSRP_MAD_FORMAT);
		break;
	case VIOSRP_ADAPTER_INFO_TYPE:
		info = &iu->mad.adapter_info;
		info->common.status = send_adapter_info(iue, info->buffer,
							info->common.length);
		send_iu(iue, sizeof(*info), VIOSRP_MAD_FORMAT);
		break;
	case VIOSRP_HOST_CONFIG_TYPE:
		conf = &iu->mad.host_config;
		conf->common.status = 1;
		send_iu(iue, sizeof(*conf), VIOSRP_MAD_FORMAT);
		break;
	default:
		pr_err("ibmvscsis: Unknown type %u\n", iu->srp.rsp.opcode);
		iu->mad.empty_iu.common.status = VIOSRP_MAD_NOT_SUPPORTED;
		send_iu(iue, sizeof(iu->mad), VIOSRP_MAD_FORMAT);
		break;
	}

	return 1;
}

static void process_login(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct srp_login_rsp *rsp = &iu->srp.login_rsp;
	uint64_t tag = iu->srp.rsp.tag;

	/*
	 * TODO handle case that requested size is wrong and buffer
	 * format is wrong
	 */
	memset(iu, 0, sizeof(struct srp_login_rsp));
	rsp->opcode = SRP_LOGIN_RSP;

	rsp->req_lim_delta = cpu_to_be32(INITIAL_SRP_LIMIT);

	rsp->tag = tag;
	rsp->max_it_iu_len = cpu_to_be32(sizeof(union srp_iu));
	rsp->max_ti_iu_len = cpu_to_be32(sizeof(union srp_iu));
	/* direct and indirect */
	rsp->buf_fmt = cpu_to_be16(SRP_BUF_FORMAT_DIRECT
					| SRP_BUF_FORMAT_INDIRECT);

	send_iu(iue, sizeof(*rsp), VIOSRP_SRP_FORMAT);
}

static void process_tsk_mgmt(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	uint64_t tag = iu->srp.rsp.tag;
	uint8_t *resp_data = iu->srp.rsp.data;

	memset(iu, 0, sizeof(struct srp_rsp));
	iu->srp.rsp.opcode = SRP_RSP;
	iu->srp.rsp.req_lim_delta = 1;
	iu->srp.rsp.tag = tag;

	iu->srp.rsp.data_in_res_cnt = 0;
	iu->srp.rsp.data_out_res_cnt = 0;

	iu->srp.rsp.flags &= ~SRP_RSP_FLAG_RSPVALID;

	iu->srp.rsp.resp_data_len = 4;
	/* TASK MANAGEMENT FUNCTION NOT SUPPORTED for now */
	resp_data[3] = 4;

	send_iu(iue, sizeof(iu->srp.rsp) + iu->srp.rsp.resp_data_len,
		VIOSRP_SRP_FORMAT);
}

static int process_srp_iu(struct iu_entry *iue)
{
	union viosrp_iu *iu = vio_iu(iue);
	struct srp_target *target = iue->target;
	int done = 1;
	u8 opcode = iu->srp.rsp.opcode;
	unsigned long flags;

	switch (opcode) {
	case SRP_LOGIN_REQ:
		pr_debug("ibmvscsis: srploginreq");
		process_login(iue);
		break;
	case SRP_TSK_MGMT:
		pr_debug("ibmvscsis: srp task mgmt");
		process_tsk_mgmt(iue);
		break;
	case SRP_CMD:
		pr_debug("ibmvscsis:srpcmd");
		spin_lock_irqsave(&target->lock, flags);
		list_add_tail(&iue->ilist, &target->cmd_queue);
		spin_unlock_irqrestore(&target->lock, flags);
		done = 0;
		break;
	case SRP_LOGIN_RSP:
	case SRP_I_LOGOUT:
	case SRP_T_LOGOUT:
	case SRP_RSP:
	case SRP_CRED_REQ:
	case SRP_CRED_RSP:
	case SRP_AER_REQ:
	case SRP_AER_RSP:
		pr_err("ibmvscsis: Unsupported type %u\n", opcode);
		break;
	default:
		pr_err("ibmvscsis: Unknown type %u\n", opcode);
	}

	return done;
}

static void process_iu(struct viosrp_crq *crq,
		       struct ibmvscsis_adapter *adapter)
{
	struct iu_entry *iue;
	long err;

	iue = srp_iu_get(adapter->target);
	if (!iue) {
		pr_err("Error getting IU from pool %p\n", iue);
		return;
	}

	iue->remote_token = crq->IU_data_ptr;

	err = h_copy_rdma(be16_to_cpu(crq->IU_length), adapter->riobn,
				be64_to_cpu(crq->IU_data_ptr),
				adapter->liobn, iue->sbuf->dma);

	if (err != H_SUCCESS) {
		pr_err("ibmvscsis: %ld transferring data error %p\n", err, iue);
		srp_iu_put(iue);
	}

	if (crq->format == VIOSRP_MAD_FORMAT) {
		process_mad_iu(iue);
	}
	else {
		pr_debug("ibmvscsis: process srpiu");
		process_srp_iu(iue);
	}
}

static void process_crq(struct viosrp_crq *crq,
			struct ibmvscsis_adapter *adapter)
{
	switch (crq->valid) {
	case 0xC0:
		/* initialization */
		switch (crq->format) {
		case 0x01:
			h_send_crq(adapter->dma_dev->unit_address,
				   0xC002000000000000, 0);
			break;
		case 0x02:
			break;
		default:
			pr_err("ibmvscsis: Unknown format %u\n", crq->format);
		}
		break;
	case 0xFF:
		/* transport event */
		break;
	case 0x80:
		/* real payload */
		switch (crq->format) {
		case VIOSRP_SRP_FORMAT:
		case VIOSRP_MAD_FORMAT:
			pr_debug("ibmvscsis: case viosrp mad crq: 0x%x, 0x%x, \
					0x%x, 0x%x, 0x%x, 0x%x, 0x%llx\n",
					crq->valid, crq->format, crq->reserved,
					crq->status, be16_to_cpu(crq->timeout),
					be16_to_cpu(crq->IU_length),
					be64_to_cpu(crq->IU_data_ptr));
			process_iu(crq, adapter);
			break;
		case VIOSRP_OS400_FORMAT:
		case VIOSRP_AIX_FORMAT:
		case VIOSRP_LINUX_FORMAT:
		case VIOSRP_INLINE_FORMAT:
			pr_err("ibmvscsis: Unsupported format %u\n",
					crq->format);
			break;
		default:
			pr_err("ibmvscsis: Unknown format %u\n",
					crq->format);
		}
		break;
	default:
		pr_err("ibmvscsis: unknown message type 0x%02x!?\n",
				crq->valid);
	}
}

static inline struct viosrp_crq *next_crq(struct crq_queue *queue)
{
	struct viosrp_crq *crq;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
	} else
		crq = NULL;
	spin_unlock_irqrestore(&queue->lock, flags);

	return crq;
}
//TODO: Needs to be rewritten to support TCM
static int tcm_queuecommand(struct ibmvscsis_adapter *adapter,
			    struct ibmvscsis_cmnd *vsc,
			    struct srp_cmd *scmd)
{
	struct se_cmd *se_cmd;
	int attr;
	int data_len;
	sense_reason_t ret;

	switch (scmd->task_attr) {
	case SRP_SIMPLE_TASK:
		attr = TCM_SIMPLE_TAG;
		break;
	case SRP_ORDERED_TASK:
		attr = TCM_ORDERED_TAG;
		break;
	case SRP_HEAD_TASK:
		attr = TCM_HEAD_TAG;
		break;
	default:
		pr_err("ibmvscsis: Task attribute %d not supported\n",
		       scmd->task_attr);
		attr = TCM_SIMPLE_TAG;
	}

	data_len = srp_data_length(scmd, srp_cmd_direction(scmd));

	se_cmd = &vsc->se_cmd;

	transport_init_se_cmd(se_cmd,
			      adapter->tpg->se_tpg.se_tpg_tfo,
			      adapter->tpg->tpg_nexus->se_sess, data_len,
			      srp_cmd_direction(scmd),
			      attr, vsc->sense_buf);

	ret = transport_lookup_cmd_lun(se_cmd, scsilun_to_int(&scmd->lun));
	if (ret) {
		transport_send_check_condition_and_sense(se_cmd,
							 ret,
							 0);
		return -1;
	}

	/*
	 * Allocate the necessary tasks to complete the received CDB+data
	 */
	ret = target_setup_cmd_from_cdb(se_cmd, scmd->cdb);
	if (ret == -ENOMEM) {
		transport_send_check_condition_and_sense(se_cmd,
				TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE, 0);
		transport_generic_free_cmd(se_cmd, 0);
		return 0;
	}
	if (ret == -EINVAL) {
		if (se_cmd->se_cmd_flags & SCF_SE_LUN_CMD)
			ibmvscsis_queue_status(se_cmd);
		else
			transport_send_check_condition_and_sense(se_cmd,
					ret, 0);
		transport_generic_free_cmd(se_cmd, 0);
		return 0;
	}

	transport_handle_cdb_direct(se_cmd);
	return 0;
}

struct inquiry_data {
	u8 qual_type;
	u8 rmb_reserve;
	u8 version;
	u8 aerc_naca_hisup_format;
	u8 addl_len;
	u8 sccs_reserved;
	u8 bque_encserv_vs_multip_mchngr_reserved;
	u8 reladr_reserved_linked_cmdqueue_vs;
	char vendor[8];
	char product[16];
	char revision[4];
	char vendor_specific[20];
	char reserved1[2];
	char version_descriptor[16];
	char reserved2[22];
	char unique[158];
};

//TODO: Need to rewrite make lun to support little endian
static u64 make_lun(unsigned int bus, unsigned int target, unsigned int lun)
{
	u16 result = (0x8000 |
			   ((target & 0x003f) << 8) |
			   ((bus & 0x0007) << 5) |
			   (lun & 0x001f));
	return ((u64) result) << 48;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_inquiry(struct ibmvscsis_adapter *adapter,
			      struct srp_cmd *cmd, char *data)
{
	struct se_portal_group *se_tpg = &adapter->tpg->se_tpg;
	struct inquiry_data *id = (struct inquiry_data *)data;
	u64 unpacked_lun, lun = scsilun_to_int(&cmd->lun);
	u8 *cdb = cmd->cdb;
	int len;
	struct se_lun *se_lun;
	int found_lun = 0;

	if (!data)
		pr_err("ibmvscsis: %s %d: oomu\n", __func__, __LINE__);

	if (((cdb[1] & 0x3) == 0x3) || (!(cdb[1] & 0x3) && cdb[2])) {
		pr_err("ibmvscsis: %s %d: invalid req\n", __func__, __LINE__);
		return 0;
	}

	if (cdb[1] & 0x3)
		pr_err("ibmvscsis: %s %d: needs the normal path\n",
		       __func__, __LINE__);
	else {
		id->qual_type = TYPE_DISK;
		id->rmb_reserve = 0x00;
		id->version = 0x84; /* ISO/IE */
		id->aerc_naca_hisup_format = 0x22; /* naca & fmt 0x02 */
		id->addl_len = sizeof(*id) - 4;
		id->bque_encserv_vs_multip_mchngr_reserved = 0x00;
		id->reladr_reserved_linked_cmdqueue_vs = 0x02; /* CMDQ */
		memcpy(id->vendor, "IBM	    ", 8);
		/*
		 * Don't even ask about the next bit.  AIX uses
		 * hardcoded device naming to recognize device types
		 * and their client won't  work unless we use VOPTA and
		 * VDASD.
		 */
		if (id->qual_type == TYPE_ROM)
			memcpy(id->product, "VOPTA blkdev    ", 16);
		else
			memcpy(id->product, "VDASD blkdev    ", 16);

		memcpy(id->revision, "0001", 4);

		snprintf(id->unique, sizeof(id->unique),
			 "IBM-VSCSI-%s-P%d-%x-%d-%d-%d\n",
			 system_id,
			 partition_number,
			 adapter->dma_dev->unit_address,
			 GETBUS(lun),
			 GETTARGET(lun),
			 GETLUN(lun));
	}

	len = min_t(int, sizeof(*id), cdb[4]);

	unpacked_lun = scsilun_to_int(&cmd->lun);

	mutex_lock(&se_tpg->tpg_lun_mutex);

	hlist_for_each_entry(se_lun, &se_tpg->tpg_lun_hlist, link) {
		if (se_lun->unpacked_lun == unpacked_lun) {
			found_lun = 1;
			break;
		}
	}

	mutex_unlock(&se_tpg->tpg_lun_mutex);

	if (!found_lun) {
		data[0] = TYPE_NO_LUN;
	}

	return len;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_mode_sense(struct ibmvscsis_adapter *adapter,
				struct srp_cmd *cmd, char *mode)
{
	int bytes = 0;
	struct se_portal_group *se_tpg = &adapter->tpg->se_tpg;
	u64 unpacked_lun;
	struct se_lun *lun = NULL;
	u32 blocks = 0 ;

	unpacked_lun = scsilun_to_int(&cmd->lun);

	mutex_lock(&se_tpg->tpg_lun_mutex);

	hlist_for_each_entry(lun, &se_tpg->tpg_lun_hlist, link) {
		if (lun->unpacked_lun == unpacked_lun) {
			// TODO get_blocks seems to be private...
			blocks = lun->lun_se_dev->transport->get_blocks(
				lun->lun_se_dev);
			break;
		}
	}

	mutex_unlock(&se_tpg->tpg_lun_mutex);

	switch (cmd->cdb[2]) {
	case 0:
	case 0x3f:
		/* Default Medium*/
		mode[1] = 0x00;
		/* if (iue->req.vd->b.ro) */
		if (0)
			mode[2] = 0x80; /* device specific */
		else
			mode[2] = 0x00; /*device specific */

		/* note the DPOFUA bit is set to zero */
		mode[3] = 0x08; /* block descriptor length */
		*((u32 *) &mode[4]) = blocks - 1;
		*((u32 *) &mode[8]) = 512;
		bytes = mode[0] = 12; /* length */
		break;
	/* cache page */
	case 0x08:
		mode[1] = 0x00; /* Default Medium */
		if (0)
			mode[2] = 0x80;
		else
			mode[2] = 0x00;

		mode[3] = 0x08;
		*((u32 *) &mode[4]) = blocks - 1;
		*((u32 *) &mode[8]) = 512;

		/* cache page */
		mode[12] = 0x08; /* page */
		mode[13] = 0x12; /* page length */
		mode[14] = 0x01; /* no cache (0x04 for read/write cache) */

		bytes = mode[0] = 12 + mode[13]; /* length */
		break;
	}

	return bytes;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_report_luns(struct ibmvscsis_adapter *adapter,
				 struct srp_cmd *cmd, u64 *data)
{
	u64 lun;
	struct se_portal_group *se_tpg = &adapter->tpg->se_tpg;
	int idx;
	int alen, oalen, nr_luns, rbuflen = 4096;
	struct se_lun *se_lun;

	alen = get_unaligned_be32(&cmd->cdb[6]);

	alen &= ~(8 - 1);
	oalen = alen;

	if (scsilun_to_int(&cmd->lun)) {
		nr_luns = 1;
		goto done;
	}

	alen -= 8;
	rbuflen -= 8; /* FIXME */
	idx = 2;
	nr_luns = 1;

	mutex_lock(&se_tpg->tpg_lun_mutex);
	// TODO Is lun_index the right thing?
	hlist_for_each_entry(se_lun, &se_tpg->tpg_lun_hlist, link) {
		lun = make_lun(0, se_lun->lun_index & 0x003f, 0);
		data[idx++] = cpu_to_be64(lun);
		alen -= 8;
		if (!alen)
			break;
		rbuflen -= 8;
		if (!rbuflen)
			break;

		nr_luns++;
	}
	mutex_unlock(&se_tpg->tpg_lun_mutex);
done:
	put_unaligned_be32(nr_luns * 8, data);
	return min(oalen, nr_luns * 8 + 8);
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_rdma(struct scsi_cmnd *sc, struct scatterlist *sg, int nsg,
			  struct srp_direct_buf *md, int nmd,
			  enum dma_data_direction dir, unsigned int rest)
{
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_target *target = iue->target;
	struct ibmvscsis_adapter *adapter = target->ldata;
	struct scatterlist *sgp = sg;
	dma_addr_t token;
	long err;
	unsigned int done = 0;
	int i, sidx, soff;

	sidx = soff = 0;
	token = sg_dma_address(sgp);

	for (i = 0; i < nmd && rest; i++) {
		unsigned int mdone, mlen;

		mlen = min(rest, be32_to_cpu(md[i].len));
		for (mdone = 0; mlen;) {
			int slen = min(sg_dma_len(sgp) - soff, mlen);

			if (dir == DMA_TO_DEVICE)
				err = h_copy_rdma(slen,
						  adapter->riobn,
						  be64_to_cpu(md[i].va) + mdone,
						  adapter->liobn,
						  token + soff);
			else
				err = h_copy_rdma(slen,
						  adapter->liobn,
						  token + soff,
						  adapter->riobn,
						  be64_to_cpu(md[i].va)+mdone);

			if (err != H_SUCCESS) {
				pr_err("ibmvscsis: rdma error %d %d %ld\n",
				       dir, slen, err);
				return -EIO;
			}

			mlen -= slen;
			mdone += slen;
			soff += slen;
			done += slen;

			if (soff == sg_dma_len(sgp)) {
				sidx++;
				sgp = sg_next(sgp);
				soff = 0;
				token = sg_dma_address(sgp);

				if (sidx > nsg) {
					pr_err("ibmvscsis: out of iue %p \
						sgp %p %d %d\n",
						iue, sgp, sidx, nsg);
					return -EIO;
				}
			}
		};

		rest -= mlen;
	}
	return 0;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_cmnd_done(struct scsi_cmnd *sc)
{
	unsigned long flags;
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	struct srp_target *target = iue->target;
	int err = 0;

	if (scsi_sg_count(sc))
		err = srp_transfer_data(sc, &vio_iu(iue)->srp.cmd,
					ibmvscsis_rdma, 1, 1);

	spin_lock_irqsave(&target->lock, flags);
	list_del(&iue->ilist);
	spin_unlock_irqrestore(&target->lock, flags);

	if (err || sc->result != SAM_STAT_GOOD) {
		pr_err("ibmvscsis: operation failed %p %d %x\n",
		       iue, sc->result, vio_iu(iue)->srp.cmd.cdb[0]);
		send_rsp(iue, sc, HARDWARE_ERROR, 0x00);
	} else
		send_rsp(iue, sc, NO_SENSE, 0x00);

	/* done(sc); */
	srp_iu_put(iue);
	return 0;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_write_pending(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
			struct ibmvscsis_cmnd, se_cmd);
	struct scsi_cmnd *sc = &cmd->sc;
	struct iu_entry *iue = (struct iu_entry *) sc->SCp.ptr;
	int ret;

	pr_debug("ibmvscsis: ibmvscsis_write_pending\n");
	sc->sdb.table.nents = se_cmd->t_data_nents;
	sc->sdb.table.sgl = se_cmd->t_data_sg;

	ret = srp_transfer_data(sc, &vio_iu(iue)->srp.cmd,
				ibmvscsis_rdma, 1, 1);
	if (ret) {
		pr_err("ibmvscsis: srp_transfer_data() failed: %d\n", ret);
		return -EAGAIN; /* Signal QUEUE_FULL */
	}
	/*
	 * We now tell TCM to add this WRITE CDB directly into the TCM storage
	 * object execution queue.
	 */
	target_execute_cmd(se_cmd);
	return 0;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_queue_data_in(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
			struct ibmvscsis_cmnd, se_cmd);
	struct scsi_cmnd *sc = &cmd->sc;
	/*
	 * Check for overflow residual count
	 */
	pr_debug("ibmvscsis: ibmvscsis_queue_data_in\n");

	if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT)
		scsi_set_resid(sc, se_cmd->residual_count);

	sc->sdb.length = se_cmd->data_length;

	sc->sdb.table.nents = se_cmd->t_data_nents;
	sc->sdb.table.sgl = se_cmd->t_data_sg;

	/*
	 * This will call srp_transfer_data() and post the response
	 * to VIO via libsrp.
	 */
	ibmvscsis_cmnd_done(sc);
	pr_debug("ibmvscsis: queue_data_in");
	return 0;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_queue_status(struct se_cmd *se_cmd)
{
	struct ibmvscsis_cmnd *cmd = container_of(se_cmd,
					struct ibmvscsis_cmnd, se_cmd);
	struct scsi_cmnd *sc = &cmd->sc;
	/*
	 * Copy any generated SENSE data into sc->sense_buffer and
	 * set the appropiate sc->result to be translated by
	 * ibmvscsis_cmnd_done()
	 */
	pr_debug("ibmvscsis: ibmvscsis_queue_status\n");
	if (se_cmd->sense_buffer &&
	   ((se_cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) ||
	    (se_cmd->se_cmd_flags & SCF_EMULATED_TASK_SENSE))) {
		memcpy((void *)sc->sense_buffer, (void *)se_cmd->sense_buffer,
				SCSI_SENSE_BUFFERSIZE);
		sc->result = host_byte(DID_OK) | driver_byte(DRIVER_SENSE) |
				SAM_STAT_CHECK_CONDITION;
	} else
		sc->result = host_byte(DID_OK) | se_cmd->scsi_status;
	/*
	 * Finally post the response to VIO via libsrp.
	 */
	ibmvscsis_cmnd_done(sc);
	return 0;
}
//TODO: Needs to be rewritten to support TCM
static int ibmvscsis_queuecommand(struct ibmvscsis_adapter *adapter,
				  struct iu_entry *iue)
{
	int data_len;
	struct srp_cmd *cmd = iue->sbuf->buf;
	struct scsi_cmnd *sc;
	struct page *pg;
	struct ibmvscsis_cmnd *vsc;

	pr_debug("ibmvscsis: ibmvscsis_queuecommand\n");

	data_len = srp_data_length(cmd, srp_cmd_direction(cmd));

	vsc = kzalloc(sizeof(*vsc), GFP_KERNEL);
	sc = &vsc->sc;
	sc->sense_buffer = vsc->sense_buf;
	sc->cmnd = cmd->cdb;
	sc->SCp.ptr = (char *)iue;

	switch (cmd->cdb[0]) {
	case INQUIRY:
		sg_alloc_table(&sc->sdb.table, 1, GFP_KERNEL);
		pg = alloc_page(GFP_KERNEL|__GFP_ZERO);
		sc->sdb.length = ibmvscsis_inquiry(adapter, cmd,
						   page_address(pg));
		sg_set_page(sc->sdb.table.sgl, pg, sc->sdb.length, 0);
		ibmvscsis_cmnd_done(sc);
		sg_free_table(&sc->sdb.table);
		__free_page(pg);
		kfree(vsc);
		break;
	case REPORT_LUNS:
		sg_alloc_table(&sc->sdb.table, 1, GFP_KERNEL);
		pg = alloc_page(GFP_KERNEL|__GFP_ZERO);
		sc->sdb.length = ibmvscsis_report_luns(adapter, cmd,
						       page_address(pg));
		sg_set_page(sc->sdb.table.sgl, pg, sc->sdb.length, 0);
		ibmvscsis_cmnd_done(sc);
		sg_free_table(&sc->sdb.table);
		__free_page(pg);
		kfree(vsc);
		break;
	case MODE_SENSE:

		sg_alloc_table(&sc->sdb.table, 1, GFP_KERNEL);
		pg = alloc_page(GFP_KERNEL|__GFP_ZERO);
		sc->sdb.length = ibmvscsis_mode_sense(adapter,
						      cmd, page_address(pg));
		sg_set_page(sc->sdb.table.sgl, pg, sc->sdb.length, 0);
		ibmvscsis_cmnd_done(sc);
		sg_free_table(&sc->sdb.table);
		__free_page(pg);
		kfree(vsc);
		break;
	default:
		tcm_queuecommand(adapter, vsc, cmd);
		break;
	}

	return 0;
}
//TODO: Needs to be rewritten to support TCM
static void handle_cmd_queue(struct ibmvscsis_adapter *adapter)
{
	struct srp_target *target = adapter->target;
	struct iu_entry *iue;
	unsigned long flags;
	int err;
	pr_debug("ibmvscsis: entering handle_cmd_queue\n");
retry:
	spin_lock_irqsave(&target->lock, flags);

	list_for_each_entry(iue, &target->cmd_queue, ilist) {
		pr_debug("ibmvscsis: iueflag: %lx\n",iue->flags);
		if (!test_and_set_bit(V_FLYING, &iue->flags)) {
			spin_unlock_irqrestore(&target->lock, flags);
			err = ibmvscsis_queuecommand(adapter, iue);
			if (err) {
				pr_err("ibmvscsis: cannot queue iue %p %d\n",
				       iue, err);
				srp_iu_put(iue);
			}
			goto retry;
		}
	}

	spin_unlock_irqrestore(&target->lock, flags);
}

static void handle_crq(unsigned long data)
{
	struct ibmvscsis_adapter *adapter =
			(struct ibmvscsis_adapter *) data;
	struct viosrp_crq *crq;
	int done = 0;

	while (!done) {
		while ((crq = next_crq(&adapter->crq_queue)) != NULL) {
			process_crq(crq, adapter);
			crq->valid = 0x00;
		}

		vio_enable_interrupts(adapter->dma_dev);

		crq = next_crq(&adapter->crq_queue);
		if (crq) {
			vio_disable_interrupts(adapter->dma_dev);
			process_crq(crq, adapter);
			crq->valid = 0x00;
		} else
			done = 1;
	}
//	handle_cmd_queue(adapter);
}

static irqreturn_t ibmvscsis_interrupt(int dummy, void *data)
{
	struct ibmvscsis_adapter *adapter = data;

	vio_disable_interrupts(adapter->dma_dev);
	tasklet_schedule(&adapter->work_task);

	return IRQ_HANDLED;
}

static int ibmvscsis_reset_crq_queue(struct ibmvscsis_adapter *adapter)
{
	int rc = 0;
	struct vio_dev *vdev = adapter->dma_dev;
	struct crq_queue *queue = &adapter->crq_queue;

	/* Close the CRQ */
	h_free_crq(vdev->unit_address);

	/* Clean out the queue */
	memset(queue->msgs, 0x00, PAGE_SIZE);
	queue->cur = 0;

	/* And re-open it again */
	rc = h_reg_crq(vdev->unit_address, queue->msg_token,
			PAGE_SIZE);
	if (rc == 2)
		/* Adapter is good, but other end is not ready */
		pr_warn("ibmvscsis: Partner adapter not ready\n");
	else if (rc != 0)
		pr_err("ibmvscsis: couldn't register crq--rc 0x%x\n", rc);

	return rc;
}

static int crq_queue_create(struct crq_queue *queue,
				struct ibmvscsis_adapter *adapter)
{
	int retrc;
	int err;
	struct vio_dev *vdev = adapter->dma_dev;

	queue->msgs = (struct viosrp_crq *)get_zeroed_page(GFP_KERNEL);

	if (!queue->msgs)
		goto malloc_failed;

	queue->size = PAGE_SIZE / sizeof(*queue->msgs);

	queue->msg_token = dma_map_single(&vdev->dev, queue->msgs,
					  queue->size * sizeof(*queue->msgs),
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(&vdev->dev, queue->msg_token)) {
		goto map_failed;
	}

	retrc = err = h_reg_crq(vdev->unit_address, queue->msg_token,
			PAGE_SIZE);

	/* If the adapter was left active for some reason (like kexec)
	 * try freeing and re-registering
	 */
	if (err == H_RESOURCE) {
		err = ibmvscsis_reset_crq_queue(adapter);
	}
	if( err == 2 ) {
		pr_warn("ibmvscsis: Partner adapter not ready\n");
		retrc = 0;
	} else if ( err != 0 ) {
		pr_err("ibmvscsis: Error 0x%x opening virtual adapter\n", err);
		goto reg_crq_failed;
	}

	queue->cur = 0;
	spin_lock_init(&queue->lock);

	tasklet_init(&adapter->work_task, handle_crq, (unsigned long)adapter);

	err = request_irq(vdev->irq, &ibmvscsis_interrupt,
			  0, "ibmvscsis", adapter);
	if (err) {
		pr_err("ibmvscsis: Error 0x%x h_send_crq\n", err);
		goto req_irq_failed;

	}

	err = vio_enable_interrupts(vdev);
	if (err != 0 ) {
		pr_err("ibmvscsis: Error %d enabling interrupts!!!\n", err);
		goto req_irq_failed;
	}

	return retrc;

req_irq_failed:
	tasklet_kill(&adapter->work_task);
reg_crq_failed:
	dma_unmap_single(&vdev->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long) queue->msgs);
malloc_failed:
	return -1;
}

static void crq_queue_destroy(struct ibmvscsis_adapter *adapter)
{
	struct vio_dev *vdev = adapter->dma_dev;
	struct crq_queue *queue = &adapter->crq_queue;

	free_irq(vdev->irq, (void *)adapter);
	tasklet_kill(&adapter->work_task);
	h_free_crq(vdev->unit_address);
	dma_unmap_single(&adapter->dma_dev->dev, queue->msg_token,
			 queue->size * sizeof(*queue->msgs), DMA_BIDIRECTIONAL);

	free_page((unsigned long)queue->msgs);
}

/* Fill in the liobn and riobn fields on the adapter */
static int read_dma_window(struct vio_dev *vdev,
				struct ibmvscsis_adapter *adapter)
{
	const __be32 *dma_window;
	const __be32 *prop;

	/* TODO Using of_parse_dma_window would be better, but it doesn't give
	 * a way to read multiple windows without already knowing the size of
	 * a window or the number of windows
	 */
	dma_window =
		(const __be32 *)vio_get_attribute(vdev, "ibm,my-dma-window",
						NULL);
	if (!dma_window) {
		pr_err("ibmvscsis: Couldn't find ibm,my-dma-window property\n");
		return -1;
	}

	adapter->liobn = be32_to_cpu(*dma_window);
	dma_window++;

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-address-cells",
						NULL);
	if (!prop) {
		pr_warn("ibmvscsis: Couldn't find ibm, \
				#dma-address-cells property\n");
		dma_window++;
	} else
		dma_window += be32_to_cpu(*prop);

	prop = (const __be32 *)vio_get_attribute(vdev, "ibm,#dma-size-cells",
						NULL);
	if (!prop) {
		pr_warn("ibmvscsis: Couldn't find ibm,#dma-size-cells property\n");
		dma_window++;
	} else
		dma_window += be32_to_cpu(*prop);

	/* dma_window should point to the second window now */
	adapter->riobn = be32_to_cpu(*dma_window);

	return 0;
}

/**
 * ibmvscsis_probe - ibm vscsis target initialize entry point
 * @param  dev vio device struct
 * @param  id  vio device id struct
 * @return	0 - Success
 *		Non-zero - Failure
 */
static int ibmvscsis_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	int ret = 0;
	struct ibmvscsis_adapter *adapter;
	struct srp_target *target;
	struct ibmvscsis_tport *tport;
	unsigned long flags;

	pr_debug("ibmvscsis: Probe for UA 0x%x\n", vdev->unit_address);

	adapter = kzalloc(sizeof(struct ibmvscsis_adapter), GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;

	target = kzalloc(sizeof(struct srp_target), GFP_KERNEL);
	if (!target)
		goto free_adapter;

	tport = kzalloc(sizeof(struct ibmvscsis_tport), GFP_KERNEL);
	if(!tport)
		goto free_target;

	adapter->dma_dev = vdev;
	target->ldata = adapter;
	adapter->target = target;
	adapter->tport = tport;
	pr_debug("ibmvscsis: tport probe pointer:%p\n", tport);

	ret = read_dma_window(adapter->dma_dev, adapter);
	if(ret != 0) {
		goto free_tport;
	}
	pr_debug("ibmvscsis: Probe: liobn 0x%x, riobn 0x%x\n", adapter->liobn,
			adapter->riobn);

	spin_lock_irqsave(&ibmvscsis_dev_lock, flags);
	list_add_tail(&adapter->list, &ibmvscsis_dev_list);
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);

	BUILD_BUG_ON_NOT_POWER_OF_2(INITIAL_SRP_LIMIT);
	ret = srp_target_alloc(adapter->target, &vdev->dev,
				INITIAL_SRP_LIMIT,
				SRP_MAX_IU_LEN);
	if(ret) {
		pr_debug("ibmvscsis: failed target alloc ret: %d\n", ret);
	}

	ret = crq_queue_create(&adapter->crq_queue, adapter);
	if(ret) {
		pr_debug("ibmvscsis: failed crq_queue_create ret: %d\n", ret);
		goto free_srp_target;
	}

	ret = h_send_crq(adapter->dma_dev->unit_address, 0xC001000000000000LL, 0);
	if(ret) {
		pr_warn("ibmvscsis: Failed to send CRQ message\n");
		goto destroy_crq_queue;
	}

	dev_set_drvdata(&vdev->dev, adapter);
	return 0;

destroy_crq_queue:
	crq_queue_destroy(adapter);
free_srp_target:
	srp_target_free(adapter->target);
free_tport:
	kfree(tport);
free_target:
	kfree(target);
free_adapter:
	kfree(adapter);
	return ret;
}

static int ibmvscsis_remove(struct vio_dev *dev)
{
	unsigned long flags;
	struct ibmvscsis_adapter *adapter = dev_get_drvdata(&dev->dev);

	spin_lock_irqsave(&ibmvscsis_dev_lock, flags);
	list_del(&adapter->list);
	spin_unlock_irqrestore(&ibmvscsis_dev_lock, flags);

	crq_queue_destroy(adapter);
	srp_target_free(adapter->target);
	kfree(adapter);

	return 0;
}

static struct vio_device_id ibmvscsis_device_table[] = {
	{"v-scsi-host", "IBM,v-scsi-host"},
	{"", ""}
};

MODULE_DEVICE_TABLE(vio, ibmvscsis_device_table);

static struct vio_driver ibmvscsis_driver = {
	.name = ibmvscsis_driver_name,
	.id_table = ibmvscsis_device_table,
	.probe = ibmvscsis_probe,
	.remove = ibmvscsis_remove,
};

static int get_system_info(void)
{
	struct device_node *rootdn, *vdevdn;
	const char *id, *model, *name;
	const unsigned int *num;

	pr_debug("ibmvscsis: getsysteminfo");
	rootdn = of_find_node_by_path("/");
	if (!rootdn)
		return -ENOENT;

	model = of_get_property(rootdn, "model", NULL);
	id = of_get_property(rootdn, "system-id", NULL);
	if (model && id)
		snprintf(system_id, sizeof(system_id), "%s-%s", model, id);

	name = of_get_property(rootdn, "ibm,partition-name", NULL);
	if (name)
		strncpy(partition_name, name, sizeof(partition_name));

	num = of_get_property(rootdn, "ibm,partition-no", NULL);
	if (num)
		partition_number = *num;

	of_node_put(rootdn);

	vdevdn = of_find_node_by_path("/vdevice");
	if (vdevdn) {
		const unsigned *mvds;

		mvds = of_get_property(vdevdn, "ibm,max-virtual-dma-size",
				       NULL);
		if (mvds)
			max_vdma_size = *mvds;
		of_node_put(vdevdn);
	}

	return 0;
};

static const struct target_core_fabric_ops ibmvscsis_ops = {
	.module				= THIS_MODULE,
	.name				= "ibmvscsis",
	.node_acl_size			= sizeof(struct ibmvscsis_nacl),
	.max_data_sg_nents		= 1024,
	.get_fabric_name		= ibmvscsis_get_fabric_name,
	.tpg_get_wwn			= ibmvscsis_get_fabric_wwn,
	.tpg_get_tag			= ibmvscsis_get_tag,
	.tpg_get_default_depth		= ibmvscsis_get_default_depth,
	.tpg_check_demo_mode		= ibmvscsis_check_true,
	.tpg_check_demo_mode_cache	= ibmvscsis_check_true,
	.tpg_check_demo_mode_write_protect = ibmvscsis_check_false,
	.tpg_check_prod_mode_write_protect = ibmvscsis_check_false,
	.tpg_get_inst_index		= ibmvscsis_tpg_get_inst_index,
	.check_stop_free		= ibmvscsis_check_stop_free,
	.release_cmd			= ibmvscsis_release_cmd,
	.shutdown_session		= ibmvscsis_shutdown_session,
	.close_session			= ibmvscsis_close_session,
	.sess_get_index			= ibmvscsis_sess_get_index,
	.write_pending			= ibmvscsis_write_pending,
	.write_pending_status		= ibmvscsis_write_pending_status,
	.set_default_node_attributes	= ibmvscsis_set_default_node_attrs,
	.get_cmd_state			= ibmvscsis_get_cmd_state,
	.queue_data_in			= ibmvscsis_queue_data_in,
	.queue_status			= ibmvscsis_queue_status,
	.queue_tm_rsp			= ibmvscsis_queue_tm_rsp,
	.aborted_task			= ibmvscsis_aborted_task,
	/*
	 * Setup function pointers for logic in target_cor_fabric_configfs.c
	 */
	.fabric_make_wwn		= ibmvscsis_make_tport,
	.fabric_drop_wwn		= ibmvscsis_drop_tport,
	.fabric_make_tpg		= ibmvscsis_make_tpg,
	.fabric_drop_tpg		= ibmvscsis_drop_tpg,

	.tfc_wwn_attrs			= ibmvscsis_wwn_attrs,
	.tfc_tpg_base_attrs		= ibmvscsis_tpg_attrs,
};

/**
 * ibmvscsis_init() - Kernel Module initialization
 *
 * Note: vio_register_driver() registers callback functions, and atleast one
 * of those call back functions calls TCM - Linux IO Target Subsystem, thus
 * the SCSI Target template must be registered before vio_register_driver()
 * is called.
 */
static int __init ibmvscsis_init(void)
{
	int ret = -ENOMEM;

	pr_info("IBMVSCSIS fabric module %s on %s/%s"
		"on "UTS_RELEASE"\n", IBMVSCSIS_VERSION, utsname()->sysname,
		utsname()->machine);

	ret = get_system_info();
	if (ret) {
		pr_err("ibmvscsis: ret %d from get_system_info\n", ret);
		goto out;
	}

	ret = class_register(&ibmvscsis_class);
	if(ret) {
		pr_err("ibmvscsis failed class register\n");
		goto out;
	}

	pr_debug("ibmvscsis: start register template");
	ret = target_register_template(&ibmvscsis_ops);
	if (ret) {
		pr_debug("ibmvscsis: ret %d from target_register_template\n",
				ret);
		goto unregister_class;
	}

	ret = vio_register_driver(&ibmvscsis_driver);
	if (ret) {
		pr_err("ibmvscsis: ret %d from vio_register_driver\n", ret);
		goto unregister_target;
	}

	return 0;


//unregister_vio:
//	vio_unregister_driver(&ibmvscsis_driver);
unregister_target:
	target_unregister_template(&ibmvscsis_ops);
unregister_class:
	class_unregister(&ibmvscsis_class);
out:
	return ret;
};

static void ibmvscsis_exit(void)
{
	pr_info("ibmvscsis: Unregister IBM virtual SCSI driver\n");
	vio_unregister_driver(&ibmvscsis_driver);
	target_unregister_template(&ibmvscsis_ops);
	class_unregister(&ibmvscsis_class);
};

MODULE_DESCRIPTION("IBMVSCSIS fabric driver");
MODULE_AUTHOR("Bryant G. Ly");
MODULE_LICENSE("GPL");
module_init(ibmvscsis_init);
module_exit(ibmvscsis_exit);

