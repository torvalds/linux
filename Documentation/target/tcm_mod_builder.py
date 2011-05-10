#!/usr/bin/python
# The TCM v4 multi-protocol fabric module generation script for drivers/target/$NEW_MOD
#
# Copyright (c) 2010 Rising Tide Systems
# Copyright (c) 2010 Linux-iSCSI.org
#
# Author: nab@kernel.org
#
import os, sys
import subprocess as sub
import string
import re
import optparse

tcm_dir = ""

fabric_ops = []
fabric_mod_dir = ""
fabric_mod_port = ""
fabric_mod_init_port = ""

def tcm_mod_err(msg):
	print msg
	sys.exit(1)

def tcm_mod_create_module_subdir(fabric_mod_dir_var):

	if os.path.isdir(fabric_mod_dir_var) == True:
		return 1

	print "Creating fabric_mod_dir: " + fabric_mod_dir_var
	ret = os.mkdir(fabric_mod_dir_var)
	if ret:
		tcm_mod_err("Unable to mkdir " + fabric_mod_dir_var)

	return

def tcm_mod_build_FC_include(fabric_mod_dir_var, fabric_mod_name):
	global fabric_mod_port
	global fabric_mod_init_port
	buf = ""

	f = fabric_mod_dir_var + "/" + fabric_mod_name + "_base.h"
	print "Writing file: " + f

	p = open(f, 'w');
	if not p:
		tcm_mod_err("Unable to open file: " + f)

	buf = "#define " + fabric_mod_name.upper() + "_VERSION	\"v0.1\"\n"
	buf += "#define " + fabric_mod_name.upper() + "_NAMELEN	32\n"
	buf += "\n"
	buf += "struct " + fabric_mod_name + "_nacl {\n"
	buf += "	/* Binary World Wide unique Port Name for FC Initiator Nport */\n"
	buf += "	u64 nport_wwpn;\n"
	buf += "	/* ASCII formatted WWPN for FC Initiator Nport */\n"
	buf += "	char nport_name[" + fabric_mod_name.upper() + "_NAMELEN];\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_nodeacl() */\n"
	buf += "	struct se_node_acl se_node_acl;\n"
	buf += "};\n"
	buf += "\n"
	buf += "struct " + fabric_mod_name + "_tpg {\n"
	buf += "	/* FC lport target portal group tag for TCM */\n"
	buf += "	u16 lport_tpgt;\n"
	buf += "	/* Pointer back to " + fabric_mod_name + "_lport */\n"
	buf += "	struct " + fabric_mod_name + "_lport *lport;\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_tpg() */\n"
	buf += "	struct se_portal_group se_tpg;\n"
	buf += "};\n"
	buf += "\n"
	buf += "struct " + fabric_mod_name + "_lport {\n"
	buf += "	/* SCSI protocol the lport is providing */\n"
	buf += "	u8 lport_proto_id;\n"
	buf += "	/* Binary World Wide unique Port Name for FC Target Lport */\n"
	buf += "	u64 lport_wwpn;\n"
	buf += "	/* ASCII formatted WWPN for FC Target Lport */\n"
	buf += "	char lport_name[" + fabric_mod_name.upper() + "_NAMELEN];\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_lport() */\n"
	buf += "	struct se_wwn lport_wwn;\n"
	buf += "};\n"

	ret = p.write(buf)
	if ret:
		tcm_mod_err("Unable to write f: " + f)

	p.close()

	fabric_mod_port = "lport"
	fabric_mod_init_port = "nport"

	return

def tcm_mod_build_SAS_include(fabric_mod_dir_var, fabric_mod_name):
	global fabric_mod_port
	global fabric_mod_init_port
	buf = ""

	f = fabric_mod_dir_var + "/" + fabric_mod_name + "_base.h"
	print "Writing file: " + f

	p = open(f, 'w');
	if not p:
		tcm_mod_err("Unable to open file: " + f)

	buf = "#define " + fabric_mod_name.upper() + "_VERSION  \"v0.1\"\n"
	buf += "#define " + fabric_mod_name.upper() + "_NAMELEN 32\n"
	buf += "\n"
	buf += "struct " + fabric_mod_name + "_nacl {\n"
	buf += "	/* Binary World Wide unique Port Name for SAS Initiator port */\n"
	buf += "	u64 iport_wwpn;\n"
	buf += "	/* ASCII formatted WWPN for Sas Initiator port */\n"
	buf += "	char iport_name[" + fabric_mod_name.upper() + "_NAMELEN];\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_nodeacl() */\n"
	buf += "	struct se_node_acl se_node_acl;\n"
	buf += "};\n\n"
	buf += "struct " + fabric_mod_name + "_tpg {\n"
	buf += "	/* SAS port target portal group tag for TCM */\n"
	buf += "	u16 tport_tpgt;\n"
	buf += "	/* Pointer back to " + fabric_mod_name + "_tport */\n"
	buf += "	struct " + fabric_mod_name + "_tport *tport;\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_tpg() */\n"
	buf += "	struct se_portal_group se_tpg;\n"
	buf += "};\n\n"
	buf += "struct " + fabric_mod_name + "_tport {\n"
	buf += "	/* SCSI protocol the tport is providing */\n"
	buf += "	u8 tport_proto_id;\n"
	buf += "	/* Binary World Wide unique Port Name for SAS Target port */\n"
	buf += "	u64 tport_wwpn;\n"
	buf += "	/* ASCII formatted WWPN for SAS Target port */\n"
	buf += "	char tport_name[" + fabric_mod_name.upper() + "_NAMELEN];\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_tport() */\n"
	buf += "	struct se_wwn tport_wwn;\n"
	buf += "};\n"

	ret = p.write(buf)
	if ret:
		tcm_mod_err("Unable to write f: " + f)

	p.close()

	fabric_mod_port = "tport"
	fabric_mod_init_port = "iport"

	return

def tcm_mod_build_iSCSI_include(fabric_mod_dir_var, fabric_mod_name):
	global fabric_mod_port
	global fabric_mod_init_port
	buf = ""

	f = fabric_mod_dir_var + "/" + fabric_mod_name + "_base.h"
	print "Writing file: " + f

	p = open(f, 'w');
	if not p:
		tcm_mod_err("Unable to open file: " + f)

	buf = "#define " + fabric_mod_name.upper() + "_VERSION  \"v0.1\"\n"
	buf += "#define " + fabric_mod_name.upper() + "_NAMELEN 32\n"
	buf += "\n"
	buf += "struct " + fabric_mod_name + "_nacl {\n"
	buf += "	/* ASCII formatted InitiatorName */\n"
	buf += "	char iport_name[" + fabric_mod_name.upper() + "_NAMELEN];\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_nodeacl() */\n"
	buf += "	struct se_node_acl se_node_acl;\n"
	buf += "};\n\n"
	buf += "struct " + fabric_mod_name + "_tpg {\n"
	buf += "	/* iSCSI target portal group tag for TCM */\n"
	buf += "	u16 tport_tpgt;\n"
	buf += "	/* Pointer back to " + fabric_mod_name + "_tport */\n"
	buf += "	struct " + fabric_mod_name + "_tport *tport;\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_tpg() */\n"
	buf += "	struct se_portal_group se_tpg;\n"
	buf += "};\n\n"
	buf += "struct " + fabric_mod_name + "_tport {\n"
	buf += "	/* SCSI protocol the tport is providing */\n"
	buf += "	u8 tport_proto_id;\n"
	buf += "	/* ASCII formatted TargetName for IQN */\n"
	buf += "	char tport_name[" + fabric_mod_name.upper() + "_NAMELEN];\n"
	buf += "	/* Returned by " + fabric_mod_name + "_make_tport() */\n"
	buf += "	struct se_wwn tport_wwn;\n"
	buf += "};\n"

	ret = p.write(buf)
	if ret:
		tcm_mod_err("Unable to write f: " + f)

	p.close()

	fabric_mod_port = "tport"
	fabric_mod_init_port = "iport"

	return

def tcm_mod_build_base_includes(proto_ident, fabric_mod_dir_val, fabric_mod_name):

	if proto_ident == "FC":
		tcm_mod_build_FC_include(fabric_mod_dir_val, fabric_mod_name)
	elif proto_ident == "SAS":
		tcm_mod_build_SAS_include(fabric_mod_dir_val, fabric_mod_name)
	elif proto_ident == "iSCSI":
		tcm_mod_build_iSCSI_include(fabric_mod_dir_val, fabric_mod_name)
	else:
		print "Unsupported proto_ident: " + proto_ident
		sys.exit(1)

	return

def tcm_mod_build_configfs(proto_ident, fabric_mod_dir_var, fabric_mod_name):
	buf = ""

	f = fabric_mod_dir_var + "/" + fabric_mod_name + "_configfs.c"
	print "Writing file: " + f

        p = open(f, 'w');
        if not p:
                tcm_mod_err("Unable to open file: " + f)

	buf = "#include <linux/module.h>\n"
	buf += "#include <linux/moduleparam.h>\n"
	buf += "#include <linux/version.h>\n"
	buf += "#include <generated/utsrelease.h>\n"
	buf += "#include <linux/utsname.h>\n"
	buf += "#include <linux/init.h>\n"
	buf += "#include <linux/slab.h>\n"
	buf += "#include <linux/kthread.h>\n"
	buf += "#include <linux/types.h>\n"
	buf += "#include <linux/string.h>\n"
	buf += "#include <linux/configfs.h>\n"
	buf += "#include <linux/ctype.h>\n"
	buf += "#include <asm/unaligned.h>\n\n"
	buf += "#include <target/target_core_base.h>\n"
	buf += "#include <target/target_core_transport.h>\n"
	buf += "#include <target/target_core_fabric_ops.h>\n"
	buf += "#include <target/target_core_fabric_configfs.h>\n"
	buf += "#include <target/target_core_fabric_lib.h>\n"
	buf += "#include <target/target_core_device.h>\n"
	buf += "#include <target/target_core_tpg.h>\n"
	buf += "#include <target/target_core_configfs.h>\n"
	buf += "#include <target/target_core_base.h>\n"
	buf += "#include <target/configfs_macros.h>\n\n"
	buf += "#include \"" + fabric_mod_name + "_base.h\"\n"
	buf += "#include \"" + fabric_mod_name + "_fabric.h\"\n\n"

	buf += "/* Local pointer to allocated TCM configfs fabric module */\n"
	buf += "struct target_fabric_configfs *" + fabric_mod_name + "_fabric_configfs;\n\n"

	buf += "static struct se_node_acl *" + fabric_mod_name + "_make_nodeacl(\n"
	buf += "	struct se_portal_group *se_tpg,\n"
	buf += "	struct config_group *group,\n"
	buf += "	const char *name)\n"
	buf += "{\n"
	buf += "	struct se_node_acl *se_nacl, *se_nacl_new;\n"
	buf += "	struct " + fabric_mod_name + "_nacl *nacl;\n"

	if proto_ident == "FC" or proto_ident == "SAS":
		buf += "	u64 wwpn = 0;\n"

	buf += "	u32 nexus_depth;\n\n"
	buf += "	/* " + fabric_mod_name + "_parse_wwn(name, &wwpn, 1) < 0)\n"
	buf += "		return ERR_PTR(-EINVAL); */\n"
	buf += "	se_nacl_new = " + fabric_mod_name + "_alloc_fabric_acl(se_tpg);\n"
	buf += "	if (!(se_nacl_new))\n"
	buf += "		return ERR_PTR(-ENOMEM);\n"
	buf += "//#warning FIXME: Hardcoded nexus depth in " + fabric_mod_name + "_make_nodeacl()\n"
	buf += "	nexus_depth = 1;\n"
	buf += "	/*\n"
	buf += "	 * se_nacl_new may be released by core_tpg_add_initiator_node_acl()\n"
	buf += "	 * when converting a NodeACL from demo mode -> explict\n"
	buf += "	 */\n"
	buf += "	se_nacl = core_tpg_add_initiator_node_acl(se_tpg, se_nacl_new,\n"
	buf += "				name, nexus_depth);\n"
	buf += "	if (IS_ERR(se_nacl)) {\n"
	buf += "		" + fabric_mod_name + "_release_fabric_acl(se_tpg, se_nacl_new);\n"
	buf += "		return se_nacl;\n"
	buf += "	}\n"
	buf += "	/*\n"
	buf += "	 * Locate our struct " + fabric_mod_name + "_nacl and set the FC Nport WWPN\n"
	buf += "	 */\n"
	buf += "	nacl = container_of(se_nacl, struct " + fabric_mod_name + "_nacl, se_node_acl);\n"

	if proto_ident == "FC" or proto_ident == "SAS":
		buf += "	nacl->" + fabric_mod_init_port + "_wwpn = wwpn;\n"

	buf += "	/* " + fabric_mod_name + "_format_wwn(&nacl->" + fabric_mod_init_port + "_name[0], " + fabric_mod_name.upper() + "_NAMELEN, wwpn); */\n\n"
	buf += "	return se_nacl;\n"
	buf += "}\n\n"
	buf += "static void " + fabric_mod_name + "_drop_nodeacl(struct se_node_acl *se_acl)\n"
	buf += "{\n"
	buf += "	struct " + fabric_mod_name + "_nacl *nacl = container_of(se_acl,\n"
	buf += "				struct " + fabric_mod_name + "_nacl, se_node_acl);\n"
	buf += "	core_tpg_del_initiator_node_acl(se_acl->se_tpg, se_acl, 1);\n"
	buf += "	kfree(nacl);\n"
	buf += "}\n\n"

	buf += "static struct se_portal_group *" + fabric_mod_name + "_make_tpg(\n"
	buf += "	struct se_wwn *wwn,\n"
	buf += "	struct config_group *group,\n"
	buf += "	const char *name)\n"
	buf += "{\n"
	buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + "*" + fabric_mod_port + " = container_of(wwn,\n"
	buf += "			struct " + fabric_mod_name + "_" + fabric_mod_port + ", " + fabric_mod_port + "_wwn);\n\n"
	buf += "	struct " + fabric_mod_name + "_tpg *tpg;\n"
	buf += "	unsigned long tpgt;\n"
	buf += "	int ret;\n\n"
	buf += "	if (strstr(name, \"tpgt_\") != name)\n"
	buf += "		return ERR_PTR(-EINVAL);\n"
	buf += "	if (strict_strtoul(name + 5, 10, &tpgt) || tpgt > UINT_MAX)\n"
	buf += "		return ERR_PTR(-EINVAL);\n\n"
	buf += "	tpg = kzalloc(sizeof(struct " + fabric_mod_name + "_tpg), GFP_KERNEL);\n"
	buf += "	if (!(tpg)) {\n"
	buf += "		printk(KERN_ERR \"Unable to allocate struct " + fabric_mod_name + "_tpg\");\n"
	buf += "		return ERR_PTR(-ENOMEM);\n"
	buf += "	}\n"
	buf += "	tpg->" + fabric_mod_port + " = " + fabric_mod_port + ";\n"
	buf += "	tpg->" + fabric_mod_port + "_tpgt = tpgt;\n\n"
	buf += "	ret = core_tpg_register(&" + fabric_mod_name + "_fabric_configfs->tf_ops, wwn,\n"
	buf += "				&tpg->se_tpg, (void *)tpg,\n"
	buf += "				TRANSPORT_TPG_TYPE_NORMAL);\n"
	buf += "	if (ret < 0) {\n"
	buf += "		kfree(tpg);\n"
	buf += "		return NULL;\n"
	buf += "	}\n"
	buf += "	return &tpg->se_tpg;\n"
	buf += "}\n\n"
	buf += "static void " + fabric_mod_name + "_drop_tpg(struct se_portal_group *se_tpg)\n"
	buf += "{\n"
	buf += "	struct " + fabric_mod_name + "_tpg *tpg = container_of(se_tpg,\n"
	buf += "				struct " + fabric_mod_name + "_tpg, se_tpg);\n\n"
	buf += "	core_tpg_deregister(se_tpg);\n"
	buf += "	kfree(tpg);\n"
	buf += "}\n\n"

	buf += "static struct se_wwn *" + fabric_mod_name + "_make_" + fabric_mod_port + "(\n"
	buf += "	struct target_fabric_configfs *tf,\n"
	buf += "	struct config_group *group,\n"
	buf += "	const char *name)\n"
	buf += "{\n"
	buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + " *" + fabric_mod_port + ";\n"

	if proto_ident == "FC" or proto_ident == "SAS":
		buf += "	u64 wwpn = 0;\n\n"

	buf += "	/* if (" + fabric_mod_name + "_parse_wwn(name, &wwpn, 1) < 0)\n"
	buf += "		return ERR_PTR(-EINVAL); */\n\n"
	buf += "	" + fabric_mod_port + " = kzalloc(sizeof(struct " + fabric_mod_name + "_" + fabric_mod_port + "), GFP_KERNEL);\n"
	buf += "	if (!(" + fabric_mod_port + ")) {\n"
	buf += "		printk(KERN_ERR \"Unable to allocate struct " + fabric_mod_name + "_" + fabric_mod_port + "\");\n"
	buf += "		return ERR_PTR(-ENOMEM);\n"
	buf += "	}\n"

	if proto_ident == "FC" or proto_ident == "SAS":
		buf += "	" + fabric_mod_port + "->" + fabric_mod_port + "_wwpn = wwpn;\n"

	buf += "	/* " + fabric_mod_name + "_format_wwn(&" + fabric_mod_port + "->" + fabric_mod_port + "_name[0], " + fabric_mod_name.upper() + "__NAMELEN, wwpn); */\n\n"
	buf += "	return &" + fabric_mod_port + "->" + fabric_mod_port + "_wwn;\n"
	buf += "}\n\n"
	buf += "static void " + fabric_mod_name + "_drop_" + fabric_mod_port + "(struct se_wwn *wwn)\n"
	buf += "{\n"
	buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + " *" + fabric_mod_port + " = container_of(wwn,\n"
	buf += "				struct " + fabric_mod_name + "_" + fabric_mod_port + ", " + fabric_mod_port + "_wwn);\n"
	buf += "	kfree(" + fabric_mod_port + ");\n"
	buf += "}\n\n"
	buf += "static ssize_t " + fabric_mod_name + "_wwn_show_attr_version(\n"
	buf += "	struct target_fabric_configfs *tf,\n"
	buf += "	char *page)\n"
	buf += "{\n"
	buf += "	return sprintf(page, \"" + fabric_mod_name.upper() + " fabric module %s on %s/%s\"\n"
	buf += "		\"on \"UTS_RELEASE\"\\n\", " + fabric_mod_name.upper() + "_VERSION, utsname()->sysname,\n"
	buf += "		utsname()->machine);\n"
	buf += "}\n\n"
	buf += "TF_WWN_ATTR_RO(" + fabric_mod_name + ", version);\n\n"
	buf += "static struct configfs_attribute *" + fabric_mod_name + "_wwn_attrs[] = {\n"
	buf += "	&" + fabric_mod_name + "_wwn_version.attr,\n"
	buf += "	NULL,\n"
	buf += "};\n\n"

	buf += "static struct target_core_fabric_ops " + fabric_mod_name + "_ops = {\n"
	buf += "	.get_fabric_name		= " + fabric_mod_name + "_get_fabric_name,\n"
	buf += "	.get_fabric_proto_ident		= " + fabric_mod_name + "_get_fabric_proto_ident,\n"
	buf += "	.tpg_get_wwn			= " + fabric_mod_name + "_get_fabric_wwn,\n"
	buf += "	.tpg_get_tag			= " + fabric_mod_name + "_get_tag,\n"
	buf += "	.tpg_get_default_depth		= " + fabric_mod_name + "_get_default_depth,\n"
	buf += "	.tpg_get_pr_transport_id	= " + fabric_mod_name + "_get_pr_transport_id,\n"
	buf += "	.tpg_get_pr_transport_id_len	= " + fabric_mod_name + "_get_pr_transport_id_len,\n"
	buf += "	.tpg_parse_pr_out_transport_id	= " + fabric_mod_name + "_parse_pr_out_transport_id,\n"
	buf += "	.tpg_check_demo_mode		= " + fabric_mod_name + "_check_false,\n"
	buf += "	.tpg_check_demo_mode_cache	= " + fabric_mod_name + "_check_true,\n"
	buf += "	.tpg_check_demo_mode_write_protect = " + fabric_mod_name + "_check_true,\n"
	buf += "	.tpg_check_prod_mode_write_protect = " + fabric_mod_name + "_check_false,\n"
	buf += "	.tpg_alloc_fabric_acl		= " + fabric_mod_name + "_alloc_fabric_acl,\n"
	buf += "	.tpg_release_fabric_acl		= " + fabric_mod_name + "_release_fabric_acl,\n"
	buf += "	.tpg_get_inst_index		= " + fabric_mod_name + "_tpg_get_inst_index,\n"
	buf += "	.release_cmd_to_pool		= " + fabric_mod_name + "_release_cmd,\n"
	buf += "	.release_cmd_direct		= " + fabric_mod_name + "_release_cmd,\n"
	buf += "	.shutdown_session		= " + fabric_mod_name + "_shutdown_session,\n"
	buf += "	.close_session			= " + fabric_mod_name + "_close_session,\n"
	buf += "	.stop_session			= " + fabric_mod_name + "_stop_session,\n"
	buf += "	.fall_back_to_erl0		= " + fabric_mod_name + "_reset_nexus,\n"
	buf += "	.sess_logged_in			= " + fabric_mod_name + "_sess_logged_in,\n"
	buf += "	.sess_get_index			= " + fabric_mod_name + "_sess_get_index,\n"
	buf += "	.sess_get_initiator_sid		= NULL,\n"
	buf += "	.write_pending			= " + fabric_mod_name + "_write_pending,\n"
	buf += "	.write_pending_status		= " + fabric_mod_name + "_write_pending_status,\n"
	buf += "	.set_default_node_attributes	= " + fabric_mod_name + "_set_default_node_attrs,\n"
	buf += "	.get_task_tag			= " + fabric_mod_name + "_get_task_tag,\n"
	buf += "	.get_cmd_state			= " + fabric_mod_name + "_get_cmd_state,\n"
	buf += "	.new_cmd_failure		= " + fabric_mod_name + "_new_cmd_failure,\n"
	buf += "	.queue_data_in			= " + fabric_mod_name + "_queue_data_in,\n"
	buf += "	.queue_status			= " + fabric_mod_name + "_queue_status,\n"
	buf += "	.queue_tm_rsp			= " + fabric_mod_name + "_queue_tm_rsp,\n"
	buf += "	.get_fabric_sense_len		= " + fabric_mod_name + "_get_fabric_sense_len,\n"
	buf += "	.set_fabric_sense_len		= " + fabric_mod_name + "_set_fabric_sense_len,\n"
	buf += "	.is_state_remove		= " + fabric_mod_name + "_is_state_remove,\n"
	buf += "	.pack_lun			= " + fabric_mod_name + "_pack_lun,\n"
	buf += "	/*\n"
	buf += "	 * Setup function pointers for generic logic in target_core_fabric_configfs.c\n"
	buf += "	 */\n"
	buf += "	.fabric_make_wwn		= " + fabric_mod_name + "_make_" + fabric_mod_port + ",\n"
	buf += "	.fabric_drop_wwn		= " + fabric_mod_name + "_drop_" + fabric_mod_port + ",\n"
	buf += "	.fabric_make_tpg		= " + fabric_mod_name + "_make_tpg,\n"
	buf += "	.fabric_drop_tpg		= " + fabric_mod_name + "_drop_tpg,\n"
	buf += "	.fabric_post_link		= NULL,\n"
	buf += "	.fabric_pre_unlink		= NULL,\n"
	buf += "	.fabric_make_np			= NULL,\n"
	buf += "	.fabric_drop_np			= NULL,\n"
	buf += "	.fabric_make_nodeacl		= " + fabric_mod_name + "_make_nodeacl,\n"
	buf += "	.fabric_drop_nodeacl		= " + fabric_mod_name + "_drop_nodeacl,\n"
	buf += "};\n\n"

	buf += "static int " + fabric_mod_name + "_register_configfs(void)\n"
	buf += "{\n"
	buf += "	struct target_fabric_configfs *fabric;\n"
	buf += "	int ret;\n\n"
	buf += "	printk(KERN_INFO \"" + fabric_mod_name.upper() + " fabric module %s on %s/%s\"\n"
	buf += "		\" on \"UTS_RELEASE\"\\n\"," + fabric_mod_name.upper() + "_VERSION, utsname()->sysname,\n"
	buf += "		utsname()->machine);\n"
	buf += "	/*\n"
	buf += "	 * Register the top level struct config_item_type with TCM core\n"
	buf += "	 */\n"
	buf += "	fabric = target_fabric_configfs_init(THIS_MODULE, \"" + fabric_mod_name[4:] + "\");\n"
	buf += "	if (!(fabric)) {\n"
	buf += "		printk(KERN_ERR \"target_fabric_configfs_init() failed\\n\");\n"
	buf += "		return -ENOMEM;\n"
	buf += "	}\n"
	buf += "	/*\n"
	buf += "	 * Setup fabric->tf_ops from our local " + fabric_mod_name + "_ops\n"
	buf += "	 */\n"
	buf += "	fabric->tf_ops = " + fabric_mod_name + "_ops;\n"
	buf += "	/*\n"
	buf += "	 * Setup default attribute lists for various fabric->tf_cit_tmpl\n"
	buf += "	 */\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_wwn_cit.ct_attrs = " + fabric_mod_name + "_wwn_attrs;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_base_cit.ct_attrs = NULL;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_attrib_cit.ct_attrs = NULL;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_param_cit.ct_attrs = NULL;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_np_base_cit.ct_attrs = NULL;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_base_cit.ct_attrs = NULL;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_attrib_cit.ct_attrs = NULL;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_auth_cit.ct_attrs = NULL;\n"
	buf += "	TF_CIT_TMPL(fabric)->tfc_tpg_nacl_param_cit.ct_attrs = NULL;\n"
	buf += "	/*\n"
	buf += "	 * Register the fabric for use within TCM\n"
	buf += "	 */\n"
	buf += "	ret = target_fabric_configfs_register(fabric);\n"
	buf += "	if (ret < 0) {\n"
	buf += "		printk(KERN_ERR \"target_fabric_configfs_register() failed\"\n"
	buf += "				\" for " + fabric_mod_name.upper() + "\\n\");\n"
	buf += "		return ret;\n"
	buf += "	}\n"
	buf += "	/*\n"
	buf += "	 * Setup our local pointer to *fabric\n"
	buf += "	 */\n"
	buf += "	" + fabric_mod_name + "_fabric_configfs = fabric;\n"
	buf += "	printk(KERN_INFO \"" +  fabric_mod_name.upper() + "[0] - Set fabric -> " + fabric_mod_name + "_fabric_configfs\\n\");\n"
	buf += "	return 0;\n"
	buf += "};\n\n"
	buf += "static void " + fabric_mod_name + "_deregister_configfs(void)\n"
	buf += "{\n"
	buf += "	if (!(" + fabric_mod_name + "_fabric_configfs))\n"
	buf += "		return;\n\n"
	buf += "	target_fabric_configfs_deregister(" + fabric_mod_name + "_fabric_configfs);\n"
	buf += "	" + fabric_mod_name + "_fabric_configfs = NULL;\n"
	buf += "	printk(KERN_INFO \"" +  fabric_mod_name.upper() + "[0] - Cleared " + fabric_mod_name + "_fabric_configfs\\n\");\n"
	buf += "};\n\n"

	buf += "static int __init " + fabric_mod_name + "_init(void)\n"
	buf += "{\n"
	buf += "	int ret;\n\n"
	buf += "	ret = " + fabric_mod_name + "_register_configfs();\n"
	buf += "	if (ret < 0)\n"
	buf += "		return ret;\n\n"
	buf += "	return 0;\n"
	buf += "};\n\n"
	buf += "static void " + fabric_mod_name + "_exit(void)\n"
	buf += "{\n"
	buf += "	" + fabric_mod_name + "_deregister_configfs();\n"
	buf += "};\n\n"

	buf += "#ifdef MODULE\n"
	buf += "MODULE_DESCRIPTION(\"" + fabric_mod_name.upper() + " series fabric driver\");\n"
	buf += "MODULE_LICENSE(\"GPL\");\n"
	buf += "module_init(" + fabric_mod_name + "_init);\n"
	buf += "module_exit(" + fabric_mod_name + "_exit);\n"
	buf += "#endif\n"

	ret = p.write(buf)
	if ret:
		tcm_mod_err("Unable to write f: " + f)

	p.close()

	return

def tcm_mod_scan_fabric_ops(tcm_dir):

	fabric_ops_api = tcm_dir + "include/target/target_core_fabric_ops.h"

	print "Using tcm_mod_scan_fabric_ops: " + fabric_ops_api
	process_fo = 0;

	p = open(fabric_ops_api, 'r')

	line = p.readline()
	while line:
		if process_fo == 0 and re.search('struct target_core_fabric_ops {', line):
			line = p.readline()
			continue

		if process_fo == 0:
			process_fo = 1;
			line = p.readline()
			# Search for function pointer
			if not re.search('\(\*', line):
				continue

			fabric_ops.append(line.rstrip())
			continue

		line = p.readline()
		# Search for function pointer
		if not re.search('\(\*', line):
			continue

		fabric_ops.append(line.rstrip())

	p.close()
	return

def tcm_mod_dump_fabric_ops(proto_ident, fabric_mod_dir_var, fabric_mod_name):
	buf = ""
	bufi = ""

	f = fabric_mod_dir_var + "/" + fabric_mod_name + "_fabric.c"
	print "Writing file: " + f

	p = open(f, 'w')
	if not p:
		tcm_mod_err("Unable to open file: " + f)

	fi = fabric_mod_dir_var + "/" + fabric_mod_name + "_fabric.h"
	print "Writing file: " + fi

	pi = open(fi, 'w')
	if not pi:
		tcm_mod_err("Unable to open file: " + fi)

	buf = "#include <linux/slab.h>\n"
	buf += "#include <linux/kthread.h>\n"
	buf += "#include <linux/types.h>\n"
	buf += "#include <linux/list.h>\n"
	buf += "#include <linux/types.h>\n"
	buf += "#include <linux/string.h>\n"
	buf += "#include <linux/ctype.h>\n"
	buf += "#include <asm/unaligned.h>\n"
	buf += "#include <scsi/scsi.h>\n"
	buf += "#include <scsi/scsi_host.h>\n"
	buf += "#include <scsi/scsi_device.h>\n"
	buf += "#include <scsi/scsi_cmnd.h>\n"
	buf += "#include <scsi/libfc.h>\n\n"
	buf += "#include <target/target_core_base.h>\n"
	buf += "#include <target/target_core_transport.h>\n"
	buf += "#include <target/target_core_fabric_ops.h>\n"
	buf += "#include <target/target_core_fabric_lib.h>\n"
	buf += "#include <target/target_core_device.h>\n"
	buf += "#include <target/target_core_tpg.h>\n"
	buf += "#include <target/target_core_configfs.h>\n\n"
	buf += "#include \"" + fabric_mod_name + "_base.h\"\n"
	buf += "#include \"" + fabric_mod_name + "_fabric.h\"\n\n"

	buf += "int " + fabric_mod_name + "_check_true(struct se_portal_group *se_tpg)\n"
	buf += "{\n"
	buf += "	return 1;\n"
	buf += "}\n\n"
	bufi += "int " + fabric_mod_name + "_check_true(struct se_portal_group *);\n"

	buf += "int " + fabric_mod_name + "_check_false(struct se_portal_group *se_tpg)\n"
	buf += "{\n"
	buf += "	return 0;\n"
	buf += "}\n\n"
	bufi += "int " + fabric_mod_name + "_check_false(struct se_portal_group *);\n"

	total_fabric_ops = len(fabric_ops)
	i = 0

	while i < total_fabric_ops:
		fo = fabric_ops[i]
		i += 1
#		print "fabric_ops: " + fo

		if re.search('get_fabric_name', fo):
			buf += "char *" + fabric_mod_name + "_get_fabric_name(void)\n"
			buf += "{\n"
			buf += "	return \"" + fabric_mod_name[4:] + "\";\n"
			buf += "}\n\n"
			bufi += "char *" + fabric_mod_name + "_get_fabric_name(void);\n"
			continue

		if re.search('get_fabric_proto_ident', fo):
			buf += "u8 " + fabric_mod_name + "_get_fabric_proto_ident(struct se_portal_group *se_tpg)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_tpg *tpg = container_of(se_tpg,\n"
			buf += "				struct " + fabric_mod_name + "_tpg, se_tpg);\n"
			buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + " *" + fabric_mod_port + " = tpg->" + fabric_mod_port + ";\n"
			buf += "	u8 proto_id;\n\n"
			buf += "	switch (" + fabric_mod_port + "->" + fabric_mod_port + "_proto_id) {\n"
			if proto_ident == "FC":
				buf += "	case SCSI_PROTOCOL_FCP:\n"
				buf += "	default:\n"
				buf += "		proto_id = fc_get_fabric_proto_ident(se_tpg);\n"
				buf += "		break;\n"
			elif proto_ident == "SAS":
				buf += "	case SCSI_PROTOCOL_SAS:\n"
				buf += "	default:\n"
				buf += "		proto_id = sas_get_fabric_proto_ident(se_tpg);\n"
				buf += "		break;\n"
			elif proto_ident == "iSCSI":
				buf += "	case SCSI_PROTOCOL_ISCSI:\n"
				buf += "	default:\n"
				buf += "		proto_id = iscsi_get_fabric_proto_ident(se_tpg);\n"
				buf += "		break;\n"

			buf += "	}\n\n"
			buf += "	return proto_id;\n"
			buf += "}\n\n"
			bufi += "u8 " + fabric_mod_name + "_get_fabric_proto_ident(struct se_portal_group *);\n"

		if re.search('get_wwn', fo):
			buf += "char *" + fabric_mod_name + "_get_fabric_wwn(struct se_portal_group *se_tpg)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_tpg *tpg = container_of(se_tpg,\n"
			buf += "				struct " + fabric_mod_name + "_tpg, se_tpg);\n"
			buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + " *" + fabric_mod_port + " = tpg->" + fabric_mod_port + ";\n\n"
			buf += "	return &" + fabric_mod_port + "->" + fabric_mod_port + "_name[0];\n"
			buf += "}\n\n"
			bufi += "char *" + fabric_mod_name + "_get_fabric_wwn(struct se_portal_group *);\n"

		if re.search('get_tag', fo):
			buf += "u16 " + fabric_mod_name + "_get_tag(struct se_portal_group *se_tpg)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_tpg *tpg = container_of(se_tpg,\n"
			buf += "				struct " + fabric_mod_name + "_tpg, se_tpg);\n"
			buf += "	return tpg->" + fabric_mod_port + "_tpgt;\n"
			buf += "}\n\n"
			bufi += "u16 " + fabric_mod_name + "_get_tag(struct se_portal_group *);\n"

		if re.search('get_default_depth', fo):
			buf += "u32 " + fabric_mod_name + "_get_default_depth(struct se_portal_group *se_tpg)\n"
			buf += "{\n"
			buf += "	return 1;\n"
			buf += "}\n\n"
			bufi += "u32 " + fabric_mod_name + "_get_default_depth(struct se_portal_group *);\n"

		if re.search('get_pr_transport_id\)\(', fo):
			buf += "u32 " + fabric_mod_name + "_get_pr_transport_id(\n"
			buf += "	struct se_portal_group *se_tpg,\n"
			buf += "	struct se_node_acl *se_nacl,\n"
			buf += "	struct t10_pr_registration *pr_reg,\n"
			buf += "	int *format_code,\n"
			buf += "	unsigned char *buf)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_tpg *tpg = container_of(se_tpg,\n"
			buf += "				struct " + fabric_mod_name + "_tpg, se_tpg);\n"
			buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + " *" + fabric_mod_port + " = tpg->" + fabric_mod_port + ";\n"
			buf += "	int ret = 0;\n\n"
			buf += "	switch (" + fabric_mod_port + "->" + fabric_mod_port + "_proto_id) {\n"
			if proto_ident == "FC":
				buf += "	case SCSI_PROTOCOL_FCP:\n"
				buf += "	default:\n"
				buf += "		ret = fc_get_pr_transport_id(se_tpg, se_nacl, pr_reg,\n"
				buf += "					format_code, buf);\n"
				buf += "		break;\n"
			elif proto_ident == "SAS":
				buf += "	case SCSI_PROTOCOL_SAS:\n"
				buf += "	default:\n"
				buf += "		ret = sas_get_pr_transport_id(se_tpg, se_nacl, pr_reg,\n"
				buf += "					format_code, buf);\n"
				buf += "		break;\n"
			elif proto_ident == "iSCSI":
				buf += "	case SCSI_PROTOCOL_ISCSI:\n"
				buf += "	default:\n"
				buf += "		ret = iscsi_get_pr_transport_id(se_tpg, se_nacl, pr_reg,\n"
				buf += "					format_code, buf);\n"
				buf += "		break;\n"

			buf += "	}\n\n"
			buf += "	return ret;\n"
			buf += "}\n\n"
			bufi += "u32 " + fabric_mod_name + "_get_pr_transport_id(struct se_portal_group *,\n"
			bufi += "			struct se_node_acl *, struct t10_pr_registration *,\n"
			bufi += "			int *, unsigned char *);\n"

		if re.search('get_pr_transport_id_len\)\(', fo):
			buf += "u32 " + fabric_mod_name + "_get_pr_transport_id_len(\n"
			buf += "	struct se_portal_group *se_tpg,\n"
			buf += "	struct se_node_acl *se_nacl,\n"
			buf += "	struct t10_pr_registration *pr_reg,\n"
			buf += "	int *format_code)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_tpg *tpg = container_of(se_tpg,\n"
			buf += "				struct " + fabric_mod_name + "_tpg, se_tpg);\n"
			buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + " *" + fabric_mod_port + " = tpg->" + fabric_mod_port + ";\n"
			buf += "	int ret = 0;\n\n"
			buf += "	switch (" + fabric_mod_port + "->" + fabric_mod_port + "_proto_id) {\n"
			if proto_ident == "FC":
				buf += "	case SCSI_PROTOCOL_FCP:\n"
				buf += "	default:\n"
				buf += "		ret = fc_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,\n"
				buf += "					format_code);\n"
				buf += "		break;\n"
			elif proto_ident == "SAS":
				buf += "	case SCSI_PROTOCOL_SAS:\n"
				buf += "	default:\n"
				buf += "		ret = sas_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,\n"
				buf += "					format_code);\n"
				buf += "		break;\n"
			elif proto_ident == "iSCSI":
				buf += "	case SCSI_PROTOCOL_ISCSI:\n"
				buf += "	default:\n"
				buf += "		ret = iscsi_get_pr_transport_id_len(se_tpg, se_nacl, pr_reg,\n"
				buf += "					format_code);\n"
				buf += "		break;\n"


			buf += "	}\n\n"
			buf += "	return ret;\n"
			buf += "}\n\n"
			bufi += "u32 " + fabric_mod_name + "_get_pr_transport_id_len(struct se_portal_group *,\n"
			bufi += "			struct se_node_acl *, struct t10_pr_registration *,\n"
			bufi += "			int *);\n"

		if re.search('parse_pr_out_transport_id\)\(', fo):
			buf += "char *" + fabric_mod_name + "_parse_pr_out_transport_id(\n"
			buf += "	struct se_portal_group *se_tpg,\n"
			buf += "	const char *buf,\n"
			buf += "	u32 *out_tid_len,\n"
			buf += "	char **port_nexus_ptr)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_tpg *tpg = container_of(se_tpg,\n"
			buf += "				struct " + fabric_mod_name + "_tpg, se_tpg);\n"
			buf += "	struct " + fabric_mod_name + "_" + fabric_mod_port + " *" + fabric_mod_port + " = tpg->" + fabric_mod_port + ";\n"
			buf += "	char *tid = NULL;\n\n"
			buf += "	switch (" + fabric_mod_port + "->" + fabric_mod_port + "_proto_id) {\n"
			if proto_ident == "FC":
				buf += "	case SCSI_PROTOCOL_FCP:\n"
				buf += "	default:\n"
				buf += "		tid = fc_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,\n"
				buf += "					port_nexus_ptr);\n"
			elif proto_ident == "SAS":
				buf += "	case SCSI_PROTOCOL_SAS:\n"
				buf += "	default:\n"
				buf += "		tid = sas_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,\n"
				buf += "					port_nexus_ptr);\n"
			elif proto_ident == "iSCSI":
				buf += "	case SCSI_PROTOCOL_ISCSI:\n"
				buf += "	default:\n"
				buf += "		tid = iscsi_parse_pr_out_transport_id(se_tpg, buf, out_tid_len,\n"
				buf += "					port_nexus_ptr);\n"

			buf += "	}\n\n"
			buf += "	return tid;\n"
			buf += "}\n\n"
			bufi += "char *" + fabric_mod_name + "_parse_pr_out_transport_id(struct se_portal_group *,\n"
			bufi +=	"			const char *, u32 *, char **);\n"

		if re.search('alloc_fabric_acl\)\(', fo):
			buf += "struct se_node_acl *" + fabric_mod_name + "_alloc_fabric_acl(struct se_portal_group *se_tpg)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_nacl *nacl;\n\n"
			buf += "	nacl = kzalloc(sizeof(struct " + fabric_mod_name + "_nacl), GFP_KERNEL);\n"
			buf += "	if (!(nacl)) {\n"
			buf += "		printk(KERN_ERR \"Unable to alocate struct " + fabric_mod_name + "_nacl\\n\");\n"
			buf += "		return NULL;\n"
			buf += "	}\n\n"
			buf += "	return &nacl->se_node_acl;\n"
			buf += "}\n\n"
			bufi += "struct se_node_acl *" + fabric_mod_name + "_alloc_fabric_acl(struct se_portal_group *);\n"

		if re.search('release_fabric_acl\)\(', fo):
			buf += "void " + fabric_mod_name + "_release_fabric_acl(\n"
			buf += "	struct se_portal_group *se_tpg,\n"
			buf += "	struct se_node_acl *se_nacl)\n"
			buf += "{\n"
			buf += "	struct " + fabric_mod_name + "_nacl *nacl = container_of(se_nacl,\n"
			buf += "			struct " + fabric_mod_name + "_nacl, se_node_acl);\n"
			buf += "	kfree(nacl);\n"
			buf += "}\n\n"
			bufi += "void " + fabric_mod_name + "_release_fabric_acl(struct se_portal_group *,\n"
			bufi +=	"			struct se_node_acl *);\n"

		if re.search('tpg_get_inst_index\)\(', fo):
			buf += "u32 " + fabric_mod_name + "_tpg_get_inst_index(struct se_portal_group *se_tpg)\n"
			buf += "{\n"
			buf += "	return 1;\n"
			buf += "}\n\n"
			bufi += "u32 " + fabric_mod_name + "_tpg_get_inst_index(struct se_portal_group *);\n"

		if re.search('release_cmd_to_pool', fo):
			buf += "void " + fabric_mod_name + "_release_cmd(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return;\n"
			buf += "}\n\n"
			bufi += "void " + fabric_mod_name + "_release_cmd(struct se_cmd *);\n"

		if re.search('shutdown_session\)\(', fo):
			buf += "int " + fabric_mod_name + "_shutdown_session(struct se_session *se_sess)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_shutdown_session(struct se_session *);\n"

		if re.search('close_session\)\(', fo):
			buf += "void " + fabric_mod_name + "_close_session(struct se_session *se_sess)\n"
			buf += "{\n"
			buf += "	return;\n"
			buf += "}\n\n"
			bufi += "void " + fabric_mod_name + "_close_session(struct se_session *);\n"

		if re.search('stop_session\)\(', fo):
			buf += "void " + fabric_mod_name + "_stop_session(struct se_session *se_sess, int sess_sleep , int conn_sleep)\n"
			buf += "{\n"
			buf += "	return;\n"
			buf += "}\n\n"
			bufi += "void " + fabric_mod_name + "_stop_session(struct se_session *, int, int);\n"

		if re.search('fall_back_to_erl0\)\(', fo):
			buf += "void " + fabric_mod_name + "_reset_nexus(struct se_session *se_sess)\n"
			buf += "{\n"
			buf += "	return;\n"
			buf += "}\n\n"
			bufi += "void " + fabric_mod_name + "_reset_nexus(struct se_session *);\n"

		if re.search('sess_logged_in\)\(', fo):
			buf += "int " + fabric_mod_name + "_sess_logged_in(struct se_session *se_sess)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_sess_logged_in(struct se_session *);\n"

		if re.search('sess_get_index\)\(', fo):
			buf += "u32 " + fabric_mod_name + "_sess_get_index(struct se_session *se_sess)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "u32 " + fabric_mod_name + "_sess_get_index(struct se_session *);\n"

		if re.search('write_pending\)\(', fo):
			buf += "int " + fabric_mod_name + "_write_pending(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_write_pending(struct se_cmd *);\n"

		if re.search('write_pending_status\)\(', fo):
			buf += "int " + fabric_mod_name + "_write_pending_status(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_write_pending_status(struct se_cmd *);\n"

		if re.search('set_default_node_attributes\)\(', fo):
			buf += "void " + fabric_mod_name + "_set_default_node_attrs(struct se_node_acl *nacl)\n"
			buf += "{\n"
			buf += "	return;\n"
			buf += "}\n\n"
			bufi += "void " + fabric_mod_name + "_set_default_node_attrs(struct se_node_acl *);\n"

		if re.search('get_task_tag\)\(', fo):
			buf += "u32 " + fabric_mod_name + "_get_task_tag(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "u32 " + fabric_mod_name + "_get_task_tag(struct se_cmd *);\n"

		if re.search('get_cmd_state\)\(', fo):
			buf += "int " + fabric_mod_name + "_get_cmd_state(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_get_cmd_state(struct se_cmd *);\n"

		if re.search('new_cmd_failure\)\(', fo):
			buf += "void " + fabric_mod_name + "_new_cmd_failure(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return;\n"
			buf += "}\n\n"
			bufi += "void " + fabric_mod_name + "_new_cmd_failure(struct se_cmd *);\n"

		if re.search('queue_data_in\)\(', fo):
			buf += "int " + fabric_mod_name + "_queue_data_in(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_queue_data_in(struct se_cmd *);\n"

		if re.search('queue_status\)\(', fo):
			buf += "int " + fabric_mod_name + "_queue_status(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_queue_status(struct se_cmd *);\n"

		if re.search('queue_tm_rsp\)\(', fo):
			buf += "int " + fabric_mod_name + "_queue_tm_rsp(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_queue_tm_rsp(struct se_cmd *);\n"

		if re.search('get_fabric_sense_len\)\(', fo):
			buf += "u16 " + fabric_mod_name + "_get_fabric_sense_len(void)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "u16 " + fabric_mod_name + "_get_fabric_sense_len(void);\n"

		if re.search('set_fabric_sense_len\)\(', fo):
			buf += "u16 " + fabric_mod_name + "_set_fabric_sense_len(struct se_cmd *se_cmd, u32 sense_length)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "u16 " + fabric_mod_name + "_set_fabric_sense_len(struct se_cmd *, u32);\n"

		if re.search('is_state_remove\)\(', fo):
			buf += "int " + fabric_mod_name + "_is_state_remove(struct se_cmd *se_cmd)\n"
			buf += "{\n"
			buf += "	return 0;\n"
			buf += "}\n\n"
			bufi += "int " + fabric_mod_name + "_is_state_remove(struct se_cmd *);\n"

		if re.search('pack_lun\)\(', fo):
			buf += "u64 " + fabric_mod_name + "_pack_lun(unsigned int lun)\n"
			buf += "{\n"
			buf += "	WARN_ON(lun >= 256);\n"
			buf += "	/* Caller wants this byte-swapped */\n"
			buf += "	return cpu_to_le64((lun & 0xff) << 8);\n"
			buf += "}\n\n"
			bufi += "u64 " + fabric_mod_name + "_pack_lun(unsigned int);\n"


	ret = p.write(buf)
	if ret:
		tcm_mod_err("Unable to write f: " + f)

	p.close()

	ret = pi.write(bufi)
	if ret:
		tcm_mod_err("Unable to write fi: " + fi)

	pi.close()
	return

def tcm_mod_build_kbuild(fabric_mod_dir_var, fabric_mod_name):

	buf = ""
	f = fabric_mod_dir_var + "/Makefile"
	print "Writing file: " + f

	p = open(f, 'w')
	if not p:
		tcm_mod_err("Unable to open file: " + f)

	buf += fabric_mod_name + "-objs			:= " + fabric_mod_name + "_fabric.o \\\n"
	buf += "					   " + fabric_mod_name + "_configfs.o\n"
	buf += "obj-$(CONFIG_" + fabric_mod_name.upper() + ")		+= " + fabric_mod_name + ".o\n"

	ret = p.write(buf)
	if ret:
		tcm_mod_err("Unable to write f: " + f)

	p.close()
	return

def tcm_mod_build_kconfig(fabric_mod_dir_var, fabric_mod_name):

	buf = ""
	f = fabric_mod_dir_var + "/Kconfig"
	print "Writing file: " + f

	p = open(f, 'w')
	if not p:
		tcm_mod_err("Unable to open file: " + f)

	buf = "config " + fabric_mod_name.upper() + "\n"
	buf += "	tristate \"" + fabric_mod_name.upper() + " fabric module\"\n"
	buf += "	depends on TARGET_CORE && CONFIGFS_FS\n"
	buf += "	default n\n"
	buf += "	---help---\n"
	buf += "	Say Y here to enable the " + fabric_mod_name.upper() + " fabric module\n"

	ret = p.write(buf)
	if ret:
		tcm_mod_err("Unable to write f: " + f)

	p.close()
	return

def tcm_mod_add_kbuild(tcm_dir, fabric_mod_name):
	buf = "obj-$(CONFIG_" + fabric_mod_name.upper() + ")	+= " + fabric_mod_name.lower() + "/\n"
	kbuild = tcm_dir + "/drivers/target/Makefile"

	f = open(kbuild, 'a')
	f.write(buf)
	f.close()
	return

def tcm_mod_add_kconfig(tcm_dir, fabric_mod_name):
	buf = "source \"drivers/target/" + fabric_mod_name.lower() + "/Kconfig\"\n"
	kconfig = tcm_dir + "/drivers/target/Kconfig"

	f = open(kconfig, 'a')
	f.write(buf)
	f.close()
	return

def main(modname, proto_ident):
#	proto_ident = "FC"
#	proto_ident = "SAS"
#	proto_ident = "iSCSI"

	tcm_dir = os.getcwd();
	tcm_dir += "/../../"
	print "tcm_dir: " + tcm_dir
	fabric_mod_name = modname
	fabric_mod_dir = tcm_dir + "drivers/target/" + fabric_mod_name
	print "Set fabric_mod_name: " + fabric_mod_name
	print "Set fabric_mod_dir: " + fabric_mod_dir
	print "Using proto_ident: " + proto_ident

	if proto_ident != "FC" and proto_ident != "SAS" and proto_ident != "iSCSI":
		print "Unsupported proto_ident: " + proto_ident
		sys.exit(1)

	ret = tcm_mod_create_module_subdir(fabric_mod_dir)
	if ret:
		print "tcm_mod_create_module_subdir() failed because module already exists!"
		sys.exit(1)

	tcm_mod_build_base_includes(proto_ident, fabric_mod_dir, fabric_mod_name)
	tcm_mod_scan_fabric_ops(tcm_dir)
	tcm_mod_dump_fabric_ops(proto_ident, fabric_mod_dir, fabric_mod_name)
	tcm_mod_build_configfs(proto_ident, fabric_mod_dir, fabric_mod_name)
	tcm_mod_build_kbuild(fabric_mod_dir, fabric_mod_name)
	tcm_mod_build_kconfig(fabric_mod_dir, fabric_mod_name)

	input = raw_input("Would you like to add " + fabric_mod_name + "to drivers/target/Makefile..? [yes,no]: ")
	if input == "yes" or input == "y":
		tcm_mod_add_kbuild(tcm_dir, fabric_mod_name)

	input = raw_input("Would you like to add " + fabric_mod_name + "to drivers/target/Kconfig..? [yes,no]: ")
	if input == "yes" or input == "y":
		tcm_mod_add_kconfig(tcm_dir, fabric_mod_name)

	return

parser = optparse.OptionParser()
parser.add_option('-m', '--modulename', help='Module name', dest='modname',
		action='store', nargs=1, type='string')
parser.add_option('-p', '--protoident', help='Protocol Ident', dest='protoident',
		action='store', nargs=1, type='string')

(opts, args) = parser.parse_args()

mandatories = ['modname', 'protoident']
for m in mandatories:
	if not opts.__dict__[m]:
		print "mandatory option is missing\n"
		parser.print_help()
		exit(-1)

if __name__ == "__main__":

	main(str(opts.modname), opts.protoident)
