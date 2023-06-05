// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/configfs.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "tcm_remote.h"

static inline struct tcm_remote_tpg *remote_tpg(struct se_portal_group *se_tpg)
{
	return container_of(se_tpg, struct tcm_remote_tpg, remote_se_tpg);
}

static char *tcm_remote_get_endpoint_wwn(struct se_portal_group *se_tpg)
{
	/*
	 * Return the passed NAA identifier for the Target Port
	 */
	return &remote_tpg(se_tpg)->remote_hba->remote_wwn_address[0];
}

static u16 tcm_remote_get_tag(struct se_portal_group *se_tpg)
{
	/*
	 * This Tag is used when forming SCSI Name identifier in EVPD=1 0x83
	 * to represent the SCSI Target Port.
	 */
	return remote_tpg(se_tpg)->remote_tpgt;
}

static int tcm_remote_dummy_cmd_fn(struct se_cmd *se_cmd)
{
	return 0;
}

static void tcm_remote_dummy_cmd_void_fn(struct se_cmd *se_cmd)
{

}

static char *tcm_remote_dump_proto_id(struct tcm_remote_hba *remote_hba)
{
	switch (remote_hba->remote_proto_id) {
	case SCSI_PROTOCOL_SAS:
		return "SAS";
	case SCSI_PROTOCOL_SRP:
		return "SRP";
	case SCSI_PROTOCOL_FCP:
		return "FCP";
	case SCSI_PROTOCOL_ISCSI:
		return "iSCSI";
	default:
		break;
	}

	return "Unknown";
}

static int tcm_remote_port_link(
	struct se_portal_group *se_tpg,
	struct se_lun *lun)
{
	pr_debug("TCM_Remote_ConfigFS: Port Link LUN %lld Successful\n",
		 lun->unpacked_lun);
	return 0;
}

static void tcm_remote_port_unlink(
	struct se_portal_group *se_tpg,
	struct se_lun *lun)
{
	pr_debug("TCM_Remote_ConfigFS: Port Unlink LUN %lld Successful\n",
		 lun->unpacked_lun);
}

static struct se_portal_group *tcm_remote_make_tpg(
	struct se_wwn *wwn,
	const char *name)
{
	struct tcm_remote_hba *remote_hba = container_of(wwn,
			struct tcm_remote_hba, remote_hba_wwn);
	struct tcm_remote_tpg *remote_tpg;
	unsigned long tpgt;
	int ret;

	if (strstr(name, "tpgt_") != name) {
		pr_err("Unable to locate \"tpgt_#\" directory group\n");
		return ERR_PTR(-EINVAL);
	}
	if (kstrtoul(name + 5, 10, &tpgt))
		return ERR_PTR(-EINVAL);

	if (tpgt >= TL_TPGS_PER_HBA) {
		pr_err("Passed tpgt: %lu exceeds TL_TPGS_PER_HBA: %u\n",
		       tpgt, TL_TPGS_PER_HBA);
		return ERR_PTR(-EINVAL);
	}
	remote_tpg = &remote_hba->remote_hba_tpgs[tpgt];
	remote_tpg->remote_hba = remote_hba;
	remote_tpg->remote_tpgt = tpgt;
	/*
	 * Register the remote_tpg as a emulated TCM Target Endpoint
	 */
	ret = core_tpg_register(wwn, &remote_tpg->remote_se_tpg,
				remote_hba->remote_proto_id);
	if (ret < 0)
		return ERR_PTR(-ENOMEM);

	pr_debug("TCM_Remote_ConfigFS: Allocated Emulated %s Target Port %s,t,0x%04lx\n",
		 tcm_remote_dump_proto_id(remote_hba),
		 config_item_name(&wwn->wwn_group.cg_item), tpgt);
	return &remote_tpg->remote_se_tpg;
}

static void tcm_remote_drop_tpg(struct se_portal_group *se_tpg)
{
	struct se_wwn *wwn = se_tpg->se_tpg_wwn;
	struct tcm_remote_tpg *remote_tpg = container_of(se_tpg,
				struct tcm_remote_tpg, remote_se_tpg);
	struct tcm_remote_hba *remote_hba;
	unsigned short tpgt;

	remote_hba = remote_tpg->remote_hba;
	tpgt = remote_tpg->remote_tpgt;

	/*
	 * Deregister the remote_tpg as a emulated TCM Target Endpoint
	 */
	core_tpg_deregister(se_tpg);

	remote_tpg->remote_hba = NULL;
	remote_tpg->remote_tpgt = 0;

	pr_debug("TCM_Remote_ConfigFS: Deallocated Emulated %s Target Port %s,t,0x%04x\n",
		 tcm_remote_dump_proto_id(remote_hba),
		 config_item_name(&wwn->wwn_group.cg_item), tpgt);
}

static struct se_wwn *tcm_remote_make_wwn(
	struct target_fabric_configfs *tf,
	struct config_group *group,
	const char *name)
{
	struct tcm_remote_hba *remote_hba;
	char *ptr;
	int ret, off = 0;

	remote_hba = kzalloc(sizeof(*remote_hba), GFP_KERNEL);
	if (!remote_hba)
		return ERR_PTR(-ENOMEM);

	/*
	 * Determine the emulated Protocol Identifier and Target Port Name
	 * based on the incoming configfs directory name.
	 */
	ptr = strstr(name, "naa.");
	if (ptr) {
		remote_hba->remote_proto_id = SCSI_PROTOCOL_SAS;
		goto check_len;
	}
	ptr = strstr(name, "fc.");
	if (ptr) {
		remote_hba->remote_proto_id = SCSI_PROTOCOL_FCP;
		off = 3; /* Skip over "fc." */
		goto check_len;
	}
	ptr = strstr(name, "0x");
	if (ptr) {
		remote_hba->remote_proto_id = SCSI_PROTOCOL_SRP;
		off = 2; /* Skip over "0x" */
		goto check_len;
	}
	ptr = strstr(name, "iqn.");
	if (!ptr) {
		pr_err("Unable to locate prefix for emulated Target Port: %s\n",
		       name);
		ret = -EINVAL;
		goto out;
	}
	remote_hba->remote_proto_id = SCSI_PROTOCOL_ISCSI;

check_len:
	if (strlen(name) >= TL_WWN_ADDR_LEN) {
		pr_err("Emulated NAA %s Address: %s, exceeds max: %d\n",
		       name, tcm_remote_dump_proto_id(remote_hba), TL_WWN_ADDR_LEN);
		ret = -EINVAL;
		goto out;
	}
	snprintf(&remote_hba->remote_wwn_address[0], TL_WWN_ADDR_LEN, "%s", &name[off]);

	pr_debug("TCM_Remote_ConfigFS: Allocated emulated Target %s Address: %s\n",
		 tcm_remote_dump_proto_id(remote_hba), name);
	return &remote_hba->remote_hba_wwn;
out:
	kfree(remote_hba);
	return ERR_PTR(ret);
}

static void tcm_remote_drop_wwn(struct se_wwn *wwn)
{
	struct tcm_remote_hba *remote_hba = container_of(wwn,
				struct tcm_remote_hba, remote_hba_wwn);

	pr_debug("TCM_Remote_ConfigFS: Deallocating emulated Target %s Address: %s\n",
		 tcm_remote_dump_proto_id(remote_hba),
		 remote_hba->remote_wwn_address);
	kfree(remote_hba);
}

static ssize_t tcm_remote_wwn_version_show(struct config_item *item, char *page)
{
	return sprintf(page, "TCM Remote Fabric module %s\n", TCM_REMOTE_VERSION);
}

CONFIGFS_ATTR_RO(tcm_remote_wwn_, version);

static struct configfs_attribute *tcm_remote_wwn_attrs[] = {
	&tcm_remote_wwn_attr_version,
	NULL,
};

static const struct target_core_fabric_ops remote_ops = {
	.module				= THIS_MODULE,
	.fabric_name			= "remote",
	.tpg_get_wwn			= tcm_remote_get_endpoint_wwn,
	.tpg_get_tag			= tcm_remote_get_tag,
	.check_stop_free		= tcm_remote_dummy_cmd_fn,
	.release_cmd			= tcm_remote_dummy_cmd_void_fn,
	.write_pending			= tcm_remote_dummy_cmd_fn,
	.queue_data_in			= tcm_remote_dummy_cmd_fn,
	.queue_status			= tcm_remote_dummy_cmd_fn,
	.queue_tm_rsp			= tcm_remote_dummy_cmd_void_fn,
	.aborted_task			= tcm_remote_dummy_cmd_void_fn,
	.fabric_make_wwn		= tcm_remote_make_wwn,
	.fabric_drop_wwn		= tcm_remote_drop_wwn,
	.fabric_make_tpg		= tcm_remote_make_tpg,
	.fabric_drop_tpg		= tcm_remote_drop_tpg,
	.fabric_post_link		= tcm_remote_port_link,
	.fabric_pre_unlink		= tcm_remote_port_unlink,
	.tfc_wwn_attrs			= tcm_remote_wwn_attrs,
};

static int __init tcm_remote_fabric_init(void)
{
	return target_register_template(&remote_ops);
}

static void __exit tcm_remote_fabric_exit(void)
{
	target_unregister_template(&remote_ops);
}

MODULE_DESCRIPTION("TCM virtual remote target");
MODULE_AUTHOR("Dmitry Bogdanov <d.bogdanov@yadro.com>");
MODULE_LICENSE("GPL");
module_init(tcm_remote_fabric_init);
module_exit(tcm_remote_fabric_exit);
