// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "iwl-drv.h"
#include "iwl-csr.h"
#include "iwl-debug.h"
#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "iwl-agn-hw.h"
#include "fw/img.h"
#include "iwl-dbg-tlv.h"
#include "iwl-config.h"
#include "iwl-modparams.h"
#include "fw/api/alive.h"
#include "fw/api/mac.h"
#include "fw/api/mac-cfg.h"

/******************************************************************************
 *
 * module boiler plate
 *
 ******************************************************************************/

#define DRV_DESCRIPTION	"Intel(R) Wireless WiFi driver for Linux"
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("GPL");

#ifdef CONFIG_IWLWIFI_DEBUGFS
static struct dentry *iwl_dbgfs_root;
#endif

/**
 * struct iwl_drv - drv common data
 * @list: list of drv structures using this opmode
 * @fw: the iwl_fw structure
 * @op_mode: the running op_mode
 * @trans: transport layer
 * @dev: for debug prints only
 * @fw_index: firmware revision to try loading
 * @firmware_name: composite filename of ucode file to load
 * @request_firmware_complete: the firmware has been obtained from user space
 * @dbgfs_drv: debugfs root directory entry
 * @dbgfs_trans: debugfs transport directory entry
 * @dbgfs_op_mode: debugfs op_mode directory entry
 */
struct iwl_drv {
	struct list_head list;
	struct iwl_fw fw;

	struct iwl_op_mode *op_mode;
	struct iwl_trans *trans;
	struct device *dev;

	int fw_index;                   /* firmware we're trying to load */
	char firmware_name[64];         /* name of firmware file to load */

	struct completion request_firmware_complete;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct dentry *dbgfs_drv;
	struct dentry *dbgfs_trans;
	struct dentry *dbgfs_op_mode;
#endif
};

enum {
	DVM_OP_MODE,
	MVM_OP_MODE,
#if IS_ENABLED(CONFIG_IWLMLD)
	MLD_OP_MODE,
#endif
};

/* Protects the table contents, i.e. the ops pointer & drv list */
static DEFINE_MUTEX(iwlwifi_opmode_table_mtx);
static struct iwlwifi_opmode_table {
	const char *name;			/* name: iwldvm, iwlmvm, etc */
	const struct iwl_op_mode_ops *ops;	/* pointer to op_mode ops */
	struct list_head drv;		/* list of devices using this op_mode */
} iwlwifi_opmode_table[] = {		/* ops set when driver is initialized */
	[DVM_OP_MODE] = { .name = "iwldvm", .ops = NULL },
	[MVM_OP_MODE] = { .name = "iwlmvm", .ops = NULL },
#if IS_ENABLED(CONFIG_IWLMLD)
	[MLD_OP_MODE] = { .name = "iwlmld", .ops = NULL },
#endif
};

#define IWL_DEFAULT_SCAN_CHANNELS 40

/*
 * struct fw_sec: Just for the image parsing process.
 * For the fw storage we are using struct fw_desc.
 */
struct fw_sec {
	const void *data;		/* the sec data */
	size_t size;			/* section size */
	u32 offset;			/* offset of writing in the device */
};

static void iwl_free_fw_desc(struct iwl_drv *drv, struct fw_desc *desc)
{
	vfree(desc->data);
	desc->data = NULL;
	desc->len = 0;
}

static void iwl_free_fw_img(struct iwl_drv *drv, struct fw_img *img)
{
	int i;
	for (i = 0; i < img->num_sec; i++)
		iwl_free_fw_desc(drv, &img->sec[i]);
	kfree(img->sec);
}

static void iwl_dealloc_ucode(struct iwl_drv *drv)
{
	int i;

	kfree(drv->fw.dbg.dest_tlv);
	for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.conf_tlv); i++)
		kfree(drv->fw.dbg.conf_tlv[i]);
	for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.trigger_tlv); i++)
		kfree(drv->fw.dbg.trigger_tlv[i]);
	kfree(drv->fw.dbg.mem_tlv);
	kfree(drv->fw.iml);
	kfree(drv->fw.ucode_capa.cmd_versions);
	kfree(drv->fw.phy_integration_ver);
	kfree(drv->trans->dbg.pc_data);
	drv->trans->dbg.pc_data = NULL;

	for (i = 0; i < IWL_UCODE_TYPE_MAX; i++)
		iwl_free_fw_img(drv, drv->fw.img + i);

	/* clear the data for the aborted load case */
	memset(&drv->fw, 0, sizeof(drv->fw));
}

static int iwl_alloc_fw_desc(struct fw_desc *desc, struct fw_sec *sec)
{
	void *data;

	desc->data = NULL;

	if (!sec || !sec->size)
		return -EINVAL;

	data = vmalloc(sec->size);
	if (!data)
		return -ENOMEM;

	desc->len = sec->size;
	desc->offset = sec->offset;
	memcpy(data, sec->data, desc->len);
	desc->data = data;

	return 0;
}

static inline char iwl_drv_get_step(int step)
{
	if (step == SILICON_Z_STEP)
		return 'z';
	if (step == SILICON_TC_STEP)
		return 'a';
	return 'a' + step;
}

static bool iwl_drv_is_wifi7_supported(struct iwl_trans *trans)
{
	return CSR_HW_RFID_TYPE(trans->info.hw_rf_id) >= IWL_CFG_RF_TYPE_FM;
}

const char *iwl_drv_get_fwname_pre(struct iwl_trans *trans, char *buf)
{
	char mac_step, rf_step;
	const char *mac, *rf, *cdb;

	if (trans->cfg->fw_name_pre)
		return trans->cfg->fw_name_pre;

	mac_step = iwl_drv_get_step(trans->info.hw_rev_step);

	switch (CSR_HW_REV_TYPE(trans->info.hw_rev)) {
	case IWL_CFG_MAC_TYPE_PU:
		mac = "9000-pu";
		mac_step = 'b';
		break;
	case IWL_CFG_MAC_TYPE_TH:
		mac = "9260-th";
		mac_step = 'b';
		break;
	case IWL_CFG_MAC_TYPE_QU:
		mac = "Qu";
		break;
	case IWL_CFG_MAC_TYPE_CC:
		/* special case - no RF since it's fixed (discrete) */
		scnprintf(buf, FW_NAME_PRE_BUFSIZE, "iwlwifi-cc-a0");
		return buf;
	case IWL_CFG_MAC_TYPE_QUZ:
		mac = "QuZ";
		/* all QuZ use A0 firmware */
		mac_step = 'a';
		break;
	case IWL_CFG_MAC_TYPE_SO:
	case IWL_CFG_MAC_TYPE_SOF:
		mac = "so";
		mac_step = 'a';
		break;
	case IWL_CFG_MAC_TYPE_TY:
		mac = "ty";
		mac_step = 'a';
		break;
	case IWL_CFG_MAC_TYPE_MA:
		mac = "ma";
		break;
	case IWL_CFG_MAC_TYPE_BZ:
	case IWL_CFG_MAC_TYPE_BZ_W:
		mac = "bz";
		break;
	case IWL_CFG_MAC_TYPE_GL:
		mac = "gl";
		break;
	case IWL_CFG_MAC_TYPE_SC:
		mac = "sc";
		break;
	case IWL_CFG_MAC_TYPE_SC2:
		mac = "sc2";
		break;
	case IWL_CFG_MAC_TYPE_SC2F:
		mac = "sc2f";
		break;
	case IWL_CFG_MAC_TYPE_BR:
		mac = "br";
		break;
	case IWL_CFG_MAC_TYPE_DR:
		mac = "dr";
		break;
	default:
		return "unknown-mac";
	}

	rf_step = iwl_drv_get_step(CSR_HW_RFID_STEP(trans->info.hw_rf_id));

	switch (CSR_HW_RFID_TYPE(trans->info.hw_rf_id)) {
	case IWL_CFG_RF_TYPE_JF1:
	case IWL_CFG_RF_TYPE_JF2:
		rf = "jf";
		rf_step = 'b';
		break;
	case IWL_CFG_RF_TYPE_HR1:
	case IWL_CFG_RF_TYPE_HR2:
		rf = "hr";
		rf_step = 'b';
		break;
	case IWL_CFG_RF_TYPE_GF:
		rf = "gf";
		rf_step = 'a';
		break;
	case IWL_CFG_RF_TYPE_FM:
		rf = "fm";
		break;
	case IWL_CFG_RF_TYPE_WH:
		rf = "wh";
		break;
	case IWL_CFG_RF_TYPE_PE:
		rf = "pe";
		break;
	default:
		return "unknown-rf";
	}

	cdb = CSR_HW_RFID_IS_CDB(trans->info.hw_rf_id) ? "4" : "";

	scnprintf(buf, FW_NAME_PRE_BUFSIZE,
		  "iwlwifi-%s-%c0-%s%s-%c0",
		  mac, mac_step, rf, cdb, rf_step);

	return buf;
}
IWL_EXPORT_SYMBOL(iwl_drv_get_fwname_pre);

static void iwl_req_fw_callback(const struct firmware *ucode_raw,
				void *context);

static int iwl_request_firmware(struct iwl_drv *drv, bool first)
{
	const struct iwl_cfg *cfg = drv->trans->cfg;
	char _fw_name_pre[FW_NAME_PRE_BUFSIZE];
	const char *fw_name_pre;

	if (drv->trans->mac_cfg->device_family == IWL_DEVICE_FAMILY_9000 &&
	    (drv->trans->info.hw_rev_step != SILICON_B_STEP &&
	     drv->trans->info.hw_rev_step != SILICON_C_STEP)) {
		IWL_ERR(drv,
			"Only HW steps B and C are currently supported (0x%0x)\n",
			drv->trans->info.hw_rev);
		return -EINVAL;
	}

	fw_name_pre = iwl_drv_get_fwname_pre(drv->trans, _fw_name_pre);

	if (first)
		drv->fw_index = cfg->ucode_api_max;
	else
		drv->fw_index--;

	if (drv->fw_index < cfg->ucode_api_min) {
		IWL_ERR(drv, "no suitable firmware found!\n");

		if (cfg->ucode_api_min == cfg->ucode_api_max) {
			IWL_ERR(drv, "%s-%d is required\n", fw_name_pre,
				cfg->ucode_api_max);
		} else {
			IWL_ERR(drv, "minimum version required: %s-%d\n",
				fw_name_pre, cfg->ucode_api_min);
			IWL_ERR(drv, "maximum version supported: %s-%d\n",
				fw_name_pre, cfg->ucode_api_max);
		}

		IWL_ERR(drv,
			"check git://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git\n");
		return -ENOENT;
	}

	snprintf(drv->firmware_name, sizeof(drv->firmware_name), "%s-%d.ucode",
		 fw_name_pre, drv->fw_index);

	IWL_DEBUG_FW_INFO(drv, "attempting to load firmware '%s'\n",
			  drv->firmware_name);

	return request_firmware_nowait(THIS_MODULE, 1, drv->firmware_name,
				       drv->trans->dev,
				       GFP_KERNEL, drv, iwl_req_fw_callback);
}

struct fw_img_parsing {
	struct fw_sec *sec;
	int sec_counter;
};

/*
 * struct fw_sec_parsing: to extract fw section and it's offset from tlv
 */
struct fw_sec_parsing {
	__le32 offset;
	const u8 data[];
} __packed;

/**
 * struct iwl_tlv_calib_data - parse the default calib data from TLV
 *
 * @ucode_type: the uCode to which the following default calib relates.
 * @calib: default calibrations.
 */
struct iwl_tlv_calib_data {
	__le32 ucode_type;
	struct iwl_tlv_calib_ctrl calib;
} __packed;

struct iwl_firmware_pieces {
	struct fw_img_parsing img[IWL_UCODE_TYPE_MAX];

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;

	/* FW debug data parsed for driver usage */
	bool dbg_dest_tlv_init;
	const u8 *dbg_dest_ver;
	union {
		const struct iwl_fw_dbg_dest_tlv *dbg_dest_tlv;
		const struct iwl_fw_dbg_dest_tlv_v1 *dbg_dest_tlv_v1;
	};
	const struct iwl_fw_dbg_conf_tlv *dbg_conf_tlv[FW_DBG_CONF_MAX];
	size_t dbg_conf_tlv_len[FW_DBG_CONF_MAX];
	const struct iwl_fw_dbg_trigger_tlv *dbg_trigger_tlv[FW_DBG_TRIGGER_MAX];
	size_t dbg_trigger_tlv_len[FW_DBG_TRIGGER_MAX];
	struct iwl_fw_dbg_mem_seg_tlv *dbg_mem_tlv;
	size_t n_mem_tlv;
	u32 major;
};

static void alloc_sec_data(struct iwl_firmware_pieces *pieces,
			   enum iwl_ucode_type type,
			   int sec)
{
	struct fw_img_parsing *img = &pieces->img[type];
	struct fw_sec *sec_memory;
	int size = sec + 1;
	size_t alloc_size = sizeof(*img->sec) * size;

	if (img->sec && img->sec_counter >= size)
		return;

	sec_memory = krealloc(img->sec, alloc_size, GFP_KERNEL);
	if (!sec_memory)
		return;

	img->sec = sec_memory;
	img->sec_counter = size;
}

static void set_sec_data(struct iwl_firmware_pieces *pieces,
			 enum iwl_ucode_type type,
			 int sec,
			 const void *data)
{
	alloc_sec_data(pieces, type, sec);

	pieces->img[type].sec[sec].data = data;
}

static void set_sec_size(struct iwl_firmware_pieces *pieces,
			 enum iwl_ucode_type type,
			 int sec,
			 size_t size)
{
	alloc_sec_data(pieces, type, sec);

	pieces->img[type].sec[sec].size = size;
}

static size_t get_sec_size(struct iwl_firmware_pieces *pieces,
			   enum iwl_ucode_type type,
			   int sec)
{
	return pieces->img[type].sec[sec].size;
}

static void set_sec_offset(struct iwl_firmware_pieces *pieces,
			   enum iwl_ucode_type type,
			   int sec,
			   u32 offset)
{
	alloc_sec_data(pieces, type, sec);

	pieces->img[type].sec[sec].offset = offset;
}

/*
 * Gets uCode section from tlv.
 */
static int iwl_store_ucode_sec(struct fw_img_parsing *img,
			       const void *data, int size)
{
	struct fw_sec *sec;
	const struct fw_sec_parsing *sec_parse;
	size_t alloc_size;

	if (WARN_ON(!img || !data))
		return -EINVAL;

	sec_parse = (const struct fw_sec_parsing *)data;

	alloc_size = sizeof(*img->sec) * (img->sec_counter + 1);
	sec = krealloc(img->sec, alloc_size, GFP_KERNEL);
	if (!sec)
		return -ENOMEM;
	img->sec = sec;

	sec = &img->sec[img->sec_counter];

	sec->offset = le32_to_cpu(sec_parse->offset);
	sec->data = sec_parse->data;
	sec->size = size - sizeof(sec_parse->offset);

	++img->sec_counter;

	return 0;
}

static int iwl_set_default_calib(struct iwl_drv *drv, const u8 *data)
{
	const struct iwl_tlv_calib_data *def_calib =
					(const struct iwl_tlv_calib_data *)data;
	u32 ucode_type = le32_to_cpu(def_calib->ucode_type);
	if (ucode_type >= IWL_UCODE_TYPE_MAX) {
		IWL_ERR(drv, "Wrong ucode_type %u for default calibration.\n",
			ucode_type);
		return -EINVAL;
	}
	drv->fw.default_calib[ucode_type].flow_trigger =
		def_calib->calib.flow_trigger;
	drv->fw.default_calib[ucode_type].event_trigger =
		def_calib->calib.event_trigger;

	return 0;
}

static void iwl_set_ucode_api_flags(struct iwl_drv *drv, const u8 *data,
				    struct iwl_ucode_capabilities *capa)
{
	const struct iwl_ucode_api *ucode_api = (const void *)data;
	u32 api_index = le32_to_cpu(ucode_api->api_index);
	u32 api_flags = le32_to_cpu(ucode_api->api_flags);
	int i;

	if (api_index >= DIV_ROUND_UP(NUM_IWL_UCODE_TLV_API, 32)) {
		IWL_WARN(drv,
			 "api flags index %d larger than supported by driver\n",
			 api_index);
		return;
	}

	for (i = 0; i < 32; i++) {
		if (api_flags & BIT(i))
			__set_bit(i + 32 * api_index, capa->_api);
	}
}

static void iwl_set_ucode_capabilities(struct iwl_drv *drv, const u8 *data,
				       struct iwl_ucode_capabilities *capa)
{
	const struct iwl_ucode_capa *ucode_capa = (const void *)data;
	u32 api_index = le32_to_cpu(ucode_capa->api_index);
	u32 api_flags = le32_to_cpu(ucode_capa->api_capa);
	int i;

	if (api_index >= DIV_ROUND_UP(NUM_IWL_UCODE_TLV_CAPA, 32)) {
		IWL_WARN(drv,
			 "capa flags index %d larger than supported by driver\n",
			 api_index);
		return;
	}

	for (i = 0; i < 32; i++) {
		if (api_flags & BIT(i))
			__set_bit(i + 32 * api_index, capa->_capa);
	}
}

static const char *iwl_reduced_fw_name(struct iwl_drv *drv)
{
	const char *name = drv->firmware_name;

	if (strncmp(name, "iwlwifi-", 8) == 0)
		name += 8;

	return name;
}

static int iwl_parse_v1_v2_firmware(struct iwl_drv *drv,
				    const struct firmware *ucode_raw,
				    struct iwl_firmware_pieces *pieces)
{
	const struct iwl_ucode_header *ucode = (const void *)ucode_raw->data;
	u32 api_ver, hdr_size, build;
	char buildstr[25];
	const u8 *src;

	drv->fw.ucode_ver = le32_to_cpu(ucode->ver);
	api_ver = IWL_UCODE_API(drv->fw.ucode_ver);

	switch (api_ver) {
	default:
		hdr_size = 28;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(drv, "File size too small!\n");
			return -EINVAL;
		}
		build = le32_to_cpu(ucode->u.v2.build);
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v2.inst_size));
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v2.data_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v2.init_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v2.init_data_size));
		src = ucode->u.v2.data;
		break;
	case 0:
	case 1:
	case 2:
		hdr_size = 24;
		if (ucode_raw->size < hdr_size) {
			IWL_ERR(drv, "File size too small!\n");
			return -EINVAL;
		}
		build = 0;
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v1.inst_size));
		set_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v1.data_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
			     le32_to_cpu(ucode->u.v1.init_size));
		set_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
			     le32_to_cpu(ucode->u.v1.init_data_size));
		src = ucode->u.v1.data;
		break;
	}

	if (build)
		sprintf(buildstr, " build %u", build);
	else
		buildstr[0] = '\0';

	snprintf(drv->fw.fw_version,
		 sizeof(drv->fw.fw_version),
		 "%u.%u.%u.%u%s %s",
		 IWL_UCODE_MAJOR(drv->fw.ucode_ver),
		 IWL_UCODE_MINOR(drv->fw.ucode_ver),
		 IWL_UCODE_API(drv->fw.ucode_ver),
		 IWL_UCODE_SERIAL(drv->fw.ucode_ver),
		 buildstr, iwl_reduced_fw_name(drv));

	/* Verify size of file vs. image size info in file's header */

	if (ucode_raw->size != hdr_size +
	    get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST) +
	    get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA) +
	    get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST) +
	    get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA)) {

		IWL_ERR(drv,
			"uCode file size %d does not match expected size\n",
			(int)ucode_raw->size);
		return -EINVAL;
	}


	set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST, src);
	src += get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST);
	set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST,
		       IWLAGN_RTC_INST_LOWER_BOUND);
	set_sec_data(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA, src);
	src += get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA);
	set_sec_offset(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA,
		       IWLAGN_RTC_DATA_LOWER_BOUND);
	set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST, src);
	src += get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST);
	set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST,
		       IWLAGN_RTC_INST_LOWER_BOUND);
	set_sec_data(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA, src);
	src += get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA);
	set_sec_offset(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA,
		       IWLAGN_RTC_DATA_LOWER_BOUND);
	return 0;
}

static void iwl_drv_set_dump_exclude(struct iwl_drv *drv,
				     enum iwl_ucode_tlv_type tlv_type,
				     const void *tlv_data, u32 tlv_len)
{
	const struct iwl_fw_dump_exclude *fw = tlv_data;
	struct iwl_dump_exclude *excl;

	if (tlv_len < sizeof(*fw))
		return;

	if (tlv_type == IWL_UCODE_TLV_SEC_TABLE_ADDR) {
		excl = &drv->fw.dump_excl[0];

		/* second time we find this, it's for WoWLAN */
		if (excl->addr)
			excl = &drv->fw.dump_excl_wowlan[0];
	} else if (fw_has_capa(&drv->fw.ucode_capa,
			       IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG)) {
		/* IWL_UCODE_TLV_D3_KEK_KCK_ADDR is regular image */
		excl = &drv->fw.dump_excl[0];
	} else {
		/* IWL_UCODE_TLV_D3_KEK_KCK_ADDR is WoWLAN image */
		excl = &drv->fw.dump_excl_wowlan[0];
	}

	if (excl->addr)
		excl++;

	if (excl->addr) {
		IWL_DEBUG_FW_INFO(drv, "found too many excludes in fw file\n");
		return;
	}

	excl->addr = le32_to_cpu(fw->addr) & ~FW_ADDR_CACHE_CONTROL;
	excl->size = le32_to_cpu(fw->size);
}

static void iwl_parse_dbg_tlv_assert_tables(struct iwl_drv *drv,
					    const struct iwl_ucode_tlv *tlv)
{
	const struct iwl_fw_ini_region_tlv *region;
	u32 length = le32_to_cpu(tlv->length);
	u32 addr;

	if (length < offsetof(typeof(*region), special_mem) +
		     sizeof(region->special_mem))
		return;

	region = (const void *)tlv->data;
	addr = le32_to_cpu(region->special_mem.base_addr);
	addr += le32_to_cpu(region->special_mem.offset);
	addr &= ~FW_ADDR_CACHE_CONTROL;

	if (region->type != IWL_FW_INI_REGION_SPECIAL_DEVICE_MEMORY)
		return;

	switch (region->sub_type) {
	case IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_UMAC_ERROR_TABLE:
		drv->trans->dbg.umac_error_event_table = addr;
		drv->trans->dbg.error_event_table_tlv_status |=
			IWL_ERROR_EVENT_TABLE_UMAC;
		break;
	case IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_LMAC_1_ERROR_TABLE:
		drv->trans->dbg.lmac_error_event_table[0] = addr;
		drv->trans->dbg.error_event_table_tlv_status |=
			IWL_ERROR_EVENT_TABLE_LMAC1;
		break;
	case IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_LMAC_2_ERROR_TABLE:
		drv->trans->dbg.lmac_error_event_table[1] = addr;
		drv->trans->dbg.error_event_table_tlv_status |=
			IWL_ERROR_EVENT_TABLE_LMAC2;
		break;
	case IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_TCM_1_ERROR_TABLE:
		drv->trans->dbg.tcm_error_event_table[0] = addr;
		drv->trans->dbg.error_event_table_tlv_status |=
			IWL_ERROR_EVENT_TABLE_TCM1;
		break;
	case IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_TCM_2_ERROR_TABLE:
		drv->trans->dbg.tcm_error_event_table[1] = addr;
		drv->trans->dbg.error_event_table_tlv_status |=
			IWL_ERROR_EVENT_TABLE_TCM2;
		break;
	case IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_RCM_1_ERROR_TABLE:
		drv->trans->dbg.rcm_error_event_table[0] = addr;
		drv->trans->dbg.error_event_table_tlv_status |=
			IWL_ERROR_EVENT_TABLE_RCM1;
		break;
	case IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_RCM_2_ERROR_TABLE:
		drv->trans->dbg.rcm_error_event_table[1] = addr;
		drv->trans->dbg.error_event_table_tlv_status |=
			IWL_ERROR_EVENT_TABLE_RCM2;
		break;
	default:
		break;
	}
}

static int iwl_parse_tlv_firmware(struct iwl_drv *drv,
				const struct firmware *ucode_raw,
				struct iwl_firmware_pieces *pieces,
				struct iwl_ucode_capabilities *capa,
				bool *usniffer_images)
{
	const struct iwl_tlv_ucode_header *ucode = (const void *)ucode_raw->data;
	const struct iwl_ucode_tlv *tlv;
	size_t len = ucode_raw->size;
	const u8 *data;
	u32 tlv_len;
	u32 usniffer_img;
	enum iwl_ucode_tlv_type tlv_type;
	const u8 *tlv_data;
	char buildstr[25];
	u32 build, paging_mem_size;
	int num_of_cpus;
	bool usniffer_req = false;

	if (len < sizeof(*ucode)) {
		IWL_ERR(drv, "uCode has invalid length: %zd\n", len);
		return -EINVAL;
	}

	if (ucode->magic != cpu_to_le32(IWL_TLV_UCODE_MAGIC)) {
		IWL_ERR(drv, "invalid uCode magic: 0X%x\n",
			le32_to_cpu(ucode->magic));
		return -EINVAL;
	}

	drv->fw.ucode_ver = le32_to_cpu(ucode->ver);
	memcpy(drv->fw.human_readable, ucode->human_readable,
	       sizeof(drv->fw.human_readable));
	build = le32_to_cpu(ucode->build);

	if (build)
		sprintf(buildstr, " build %u", build);
	else
		buildstr[0] = '\0';

	snprintf(drv->fw.fw_version,
		 sizeof(drv->fw.fw_version),
		 "%u.%u.%u.%u%s %s",
		 IWL_UCODE_MAJOR(drv->fw.ucode_ver),
		 IWL_UCODE_MINOR(drv->fw.ucode_ver),
		 IWL_UCODE_API(drv->fw.ucode_ver),
		 IWL_UCODE_SERIAL(drv->fw.ucode_ver),
		 buildstr, iwl_reduced_fw_name(drv));

	data = ucode->data;

	len -= sizeof(*ucode);

	while (len >= sizeof(*tlv)) {
		len -= sizeof(*tlv);

		tlv = (const void *)data;
		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le32_to_cpu(tlv->type);
		tlv_data = tlv->data;

		if (len < tlv_len) {
			IWL_ERR(drv, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		switch (tlv_type) {
		case IWL_UCODE_TLV_INST:
			set_sec_data(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_INST, tlv_data);
			set_sec_size(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_INST, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_REGULAR,
				       IWL_UCODE_SECTION_INST,
				       IWLAGN_RTC_INST_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_DATA:
			set_sec_data(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_DATA, tlv_data);
			set_sec_size(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_DATA, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_REGULAR,
				       IWL_UCODE_SECTION_DATA,
				       IWLAGN_RTC_DATA_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_INIT:
			set_sec_data(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_INST, tlv_data);
			set_sec_size(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_INST, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_INIT,
				       IWL_UCODE_SECTION_INST,
				       IWLAGN_RTC_INST_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_INIT_DATA:
			set_sec_data(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_DATA, tlv_data);
			set_sec_size(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_DATA, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_INIT,
				       IWL_UCODE_SECTION_DATA,
				       IWLAGN_RTC_DATA_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_BOOT:
			IWL_ERR(drv, "Found unexpected BOOT ucode\n");
			break;
		case IWL_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->max_probe_length =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_PAN:
			if (tlv_len)
				goto invalid_tlv_len;
			capa->flags |= IWL_UCODE_TLV_FLAGS_PAN;
			break;
		case IWL_UCODE_TLV_FLAGS:
			/* must be at least one u32 */
			if (tlv_len < sizeof(u32))
				goto invalid_tlv_len;
			/* and a proper number of u32s */
			if (tlv_len % sizeof(u32))
				goto invalid_tlv_len;
			/*
			 * This driver only reads the first u32 as
			 * right now no more features are defined,
			 * if that changes then either the driver
			 * will not work with the new firmware, or
			 * it'll not take advantage of new features.
			 */
			capa->flags = le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_API_CHANGES_SET:
			if (tlv_len != sizeof(struct iwl_ucode_api))
				goto invalid_tlv_len;
			iwl_set_ucode_api_flags(drv, tlv_data, capa);
			break;
		case IWL_UCODE_TLV_ENABLED_CAPABILITIES:
			if (tlv_len != sizeof(struct iwl_ucode_capa))
				goto invalid_tlv_len;
			iwl_set_ucode_capabilities(drv, tlv_data, capa);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_ptr =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_evtlog_size =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_INIT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->init_errlog_ptr =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_ptr =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_EVTLOG_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_evtlog_size =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_RUNT_ERRLOG_PTR:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			pieces->inst_errlog_ptr =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_ENHANCE_SENS_TBL:
			if (tlv_len)
				goto invalid_tlv_len;
			drv->fw.enhance_sensitivity_table = true;
			break;
		case IWL_UCODE_TLV_WOWLAN_INST:
			set_sec_data(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_INST, tlv_data);
			set_sec_size(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_INST, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_WOWLAN,
				       IWL_UCODE_SECTION_INST,
				       IWLAGN_RTC_INST_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_WOWLAN_DATA:
			set_sec_data(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_DATA, tlv_data);
			set_sec_size(pieces, IWL_UCODE_WOWLAN,
				     IWL_UCODE_SECTION_DATA, tlv_len);
			set_sec_offset(pieces, IWL_UCODE_WOWLAN,
				       IWL_UCODE_SECTION_DATA,
				       IWLAGN_RTC_DATA_LOWER_BOUND);
			break;
		case IWL_UCODE_TLV_PHY_CALIBRATION_SIZE:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->standard_phy_calibration_size =
					le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_SEC_RT:
			iwl_store_ucode_sec(&pieces->img[IWL_UCODE_REGULAR],
					    tlv_data, tlv_len);
			drv->fw.type = IWL_FW_MVM;
			break;
		case IWL_UCODE_TLV_SEC_INIT:
			iwl_store_ucode_sec(&pieces->img[IWL_UCODE_INIT],
					    tlv_data, tlv_len);
			drv->fw.type = IWL_FW_MVM;
			break;
		case IWL_UCODE_TLV_SEC_WOWLAN:
			iwl_store_ucode_sec(&pieces->img[IWL_UCODE_WOWLAN],
					    tlv_data, tlv_len);
			drv->fw.type = IWL_FW_MVM;
			break;
		case IWL_UCODE_TLV_DEF_CALIB:
			if (tlv_len != sizeof(struct iwl_tlv_calib_data))
				goto invalid_tlv_len;
			if (iwl_set_default_calib(drv, tlv_data))
				goto tlv_error;
			break;
		case IWL_UCODE_TLV_PHY_SKU:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			drv->fw.phy_config = le32_to_cpup((const __le32 *)tlv_data);
			drv->fw.valid_tx_ant = (drv->fw.phy_config &
						FW_PHY_CFG_TX_CHAIN) >>
						FW_PHY_CFG_TX_CHAIN_POS;
			drv->fw.valid_rx_ant = (drv->fw.phy_config &
						FW_PHY_CFG_RX_CHAIN) >>
						FW_PHY_CFG_RX_CHAIN_POS;
			break;
		case IWL_UCODE_TLV_SECURE_SEC_RT:
			iwl_store_ucode_sec(&pieces->img[IWL_UCODE_REGULAR],
					    tlv_data, tlv_len);
			drv->fw.type = IWL_FW_MVM;
			break;
		case IWL_UCODE_TLV_SECURE_SEC_INIT:
			iwl_store_ucode_sec(&pieces->img[IWL_UCODE_INIT],
					    tlv_data, tlv_len);
			drv->fw.type = IWL_FW_MVM;
			break;
		case IWL_UCODE_TLV_SECURE_SEC_WOWLAN:
			iwl_store_ucode_sec(&pieces->img[IWL_UCODE_WOWLAN],
					    tlv_data, tlv_len);
			drv->fw.type = IWL_FW_MVM;
			break;
		case IWL_UCODE_TLV_NUM_OF_CPU:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			num_of_cpus =
				le32_to_cpup((const __le32 *)tlv_data);

			if (num_of_cpus == 2) {
				drv->fw.img[IWL_UCODE_REGULAR].is_dual_cpus =
					true;
				drv->fw.img[IWL_UCODE_INIT].is_dual_cpus =
					true;
				drv->fw.img[IWL_UCODE_WOWLAN].is_dual_cpus =
					true;
			} else if ((num_of_cpus > 2) || (num_of_cpus < 1)) {
				IWL_ERR(drv, "Driver support up to 2 CPUs\n");
				return -EINVAL;
			}
			break;
		case IWL_UCODE_TLV_N_SCAN_CHANNELS:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->n_scan_channels =
				le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_FW_VERSION: {
			const __le32 *ptr = (const void *)tlv_data;
			u32 minor;
			u8 local_comp;

			if (tlv_len != sizeof(u32) * 3)
				goto invalid_tlv_len;

			pieces->major = le32_to_cpup(ptr++);
			minor = le32_to_cpup(ptr++);
			local_comp = le32_to_cpup(ptr);

			snprintf(drv->fw.fw_version,
				 sizeof(drv->fw.fw_version),
				 "%u.%08x.%u %s", pieces->major, minor,
				 local_comp, iwl_reduced_fw_name(drv));
			break;
			}
		case IWL_UCODE_TLV_FW_DBG_DEST: {
			const struct iwl_fw_dbg_dest_tlv *dest = NULL;
			const struct iwl_fw_dbg_dest_tlv_v1 *dest_v1 = NULL;
			u8 mon_mode;

			pieces->dbg_dest_ver = (const u8 *)tlv_data;
			if (*pieces->dbg_dest_ver == 1) {
				dest = (const void *)tlv_data;
			} else if (*pieces->dbg_dest_ver == 0) {
				dest_v1 = (const void *)tlv_data;
			} else {
				IWL_ERR(drv,
					"The version is %d, and it is invalid\n",
					*pieces->dbg_dest_ver);
				break;
			}

			if (pieces->dbg_dest_tlv_init) {
				IWL_ERR(drv,
					"dbg destination ignored, already exists\n");
				break;
			}

			pieces->dbg_dest_tlv_init = true;

			if (dest_v1) {
				pieces->dbg_dest_tlv_v1 = dest_v1;
				mon_mode = dest_v1->monitor_mode;
			} else {
				pieces->dbg_dest_tlv = dest;
				mon_mode = dest->monitor_mode;
			}

			IWL_INFO(drv, "Found debug destination: %s\n",
				 get_fw_dbg_mode_string(mon_mode));

			drv->fw.dbg.n_dest_reg = (dest_v1) ?
				tlv_len -
				offsetof(struct iwl_fw_dbg_dest_tlv_v1,
					 reg_ops) :
				tlv_len -
				offsetof(struct iwl_fw_dbg_dest_tlv,
					 reg_ops);

			drv->fw.dbg.n_dest_reg /=
				sizeof(drv->fw.dbg.dest_tlv->reg_ops[0]);

			break;
			}
		case IWL_UCODE_TLV_FW_DBG_CONF: {
			const struct iwl_fw_dbg_conf_tlv *conf =
				(const void *)tlv_data;

			if (!pieces->dbg_dest_tlv_init) {
				IWL_ERR(drv,
					"Ignore dbg config %d - no destination configured\n",
					conf->id);
				break;
			}

			if (conf->id >= ARRAY_SIZE(drv->fw.dbg.conf_tlv)) {
				IWL_ERR(drv,
					"Skip unknown configuration: %d\n",
					conf->id);
				break;
			}

			if (pieces->dbg_conf_tlv[conf->id]) {
				IWL_ERR(drv,
					"Ignore duplicate dbg config %d\n",
					conf->id);
				break;
			}

			if (conf->usniffer)
				usniffer_req = true;

			IWL_INFO(drv, "Found debug configuration: %d\n",
				 conf->id);

			pieces->dbg_conf_tlv[conf->id] = conf;
			pieces->dbg_conf_tlv_len[conf->id] = tlv_len;
			break;
			}
		case IWL_UCODE_TLV_FW_DBG_TRIGGER: {
			const struct iwl_fw_dbg_trigger_tlv *trigger =
				(const void *)tlv_data;
			u32 trigger_id = le32_to_cpu(trigger->id);

			if (trigger_id >= ARRAY_SIZE(drv->fw.dbg.trigger_tlv)) {
				IWL_ERR(drv,
					"Skip unknown trigger: %u\n",
					trigger->id);
				break;
			}

			if (pieces->dbg_trigger_tlv[trigger_id]) {
				IWL_ERR(drv,
					"Ignore duplicate dbg trigger %u\n",
					trigger->id);
				break;
			}

			IWL_INFO(drv, "Found debug trigger: %u\n", trigger->id);

			pieces->dbg_trigger_tlv[trigger_id] = trigger;
			pieces->dbg_trigger_tlv_len[trigger_id] = tlv_len;
			break;
			}
		case IWL_UCODE_TLV_FW_DBG_DUMP_LST: {
			if (tlv_len != sizeof(u32)) {
				IWL_ERR(drv,
					"dbg lst mask size incorrect, skip\n");
				break;
			}

			drv->fw.dbg.dump_mask =
				le32_to_cpup((const __le32 *)tlv_data);
			break;
			}
		case IWL_UCODE_TLV_SEC_RT_USNIFFER:
			*usniffer_images = true;
			iwl_store_ucode_sec(&pieces->img[IWL_UCODE_REGULAR_USNIFFER],
					    tlv_data, tlv_len);
			break;
		case IWL_UCODE_TLV_PAGING:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			paging_mem_size = le32_to_cpup((const __le32 *)tlv_data);

			IWL_DEBUG_FW(drv,
				     "Paging: paging enabled (size = %u bytes)\n",
				     paging_mem_size);

			if (paging_mem_size > MAX_PAGING_IMAGE_SIZE) {
				IWL_ERR(drv,
					"Paging: driver supports up to %lu bytes for paging image\n",
					MAX_PAGING_IMAGE_SIZE);
				return -EINVAL;
			}

			if (paging_mem_size & (FW_PAGING_SIZE - 1)) {
				IWL_ERR(drv,
					"Paging: image isn't multiple %lu\n",
					FW_PAGING_SIZE);
				return -EINVAL;
			}

			drv->fw.img[IWL_UCODE_REGULAR].paging_mem_size =
				paging_mem_size;
			usniffer_img = IWL_UCODE_REGULAR_USNIFFER;
			drv->fw.img[usniffer_img].paging_mem_size =
				paging_mem_size;
			break;
		case IWL_UCODE_TLV_FW_GSCAN_CAPA:
			/* ignored */
			break;
		case IWL_UCODE_TLV_FW_MEM_SEG: {
			const struct iwl_fw_dbg_mem_seg_tlv *dbg_mem =
				(const void *)tlv_data;
			size_t size;
			struct iwl_fw_dbg_mem_seg_tlv *n;

			if (tlv_len != (sizeof(*dbg_mem)))
				goto invalid_tlv_len;

			IWL_DEBUG_INFO(drv, "Found debug memory segment: %u\n",
				       dbg_mem->data_type);

			size = sizeof(*pieces->dbg_mem_tlv) *
			       (pieces->n_mem_tlv + 1);
			n = krealloc(pieces->dbg_mem_tlv, size, GFP_KERNEL);
			if (!n)
				return -ENOMEM;
			pieces->dbg_mem_tlv = n;
			pieces->dbg_mem_tlv[pieces->n_mem_tlv] = *dbg_mem;
			pieces->n_mem_tlv++;
			break;
			}
		case IWL_UCODE_TLV_IML: {
			drv->fw.iml_len = tlv_len;
			drv->fw.iml = kmemdup(tlv_data, tlv_len, GFP_KERNEL);
			if (!drv->fw.iml)
				return -ENOMEM;
			break;
			}
		case IWL_UCODE_TLV_FW_RECOVERY_INFO: {
			const struct {
				__le32 buf_addr;
				__le32 buf_size;
			} *recov_info = (const void *)tlv_data;

			if (tlv_len != sizeof(*recov_info))
				goto invalid_tlv_len;
			capa->error_log_addr =
				le32_to_cpu(recov_info->buf_addr);
			capa->error_log_size =
				le32_to_cpu(recov_info->buf_size);
			}
			break;
		case IWL_UCODE_TLV_FW_FSEQ_VERSION: {
			const struct {
				u8 version[32];
				u8 sha1[20];
			} *fseq_ver = (const void *)tlv_data;

			if (tlv_len != sizeof(*fseq_ver))
				goto invalid_tlv_len;
			IWL_INFO(drv, "TLV_FW_FSEQ_VERSION: %.32s\n",
				 fseq_ver->version);
			}
			break;
		case IWL_UCODE_TLV_FW_NUM_STATIONS:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			if (le32_to_cpup((const __le32 *)tlv_data) >
			    IWL_STATION_COUNT_MAX) {
				IWL_ERR(drv,
					"%d is an invalid number of station\n",
					le32_to_cpup((const __le32 *)tlv_data));
				goto tlv_error;
			}
			capa->num_stations =
				le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_FW_NUM_LINKS:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			if (le32_to_cpup((const __le32 *)tlv_data) >
			    IWL_FW_MAX_LINK_ID + 1) {
				IWL_ERR(drv,
					"%d is an invalid number of links\n",
					le32_to_cpup((const __le32 *)tlv_data));
				goto tlv_error;
			}
			capa->num_links =
				le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_FW_NUM_BEACONS:
			if (tlv_len != sizeof(u32))
				goto invalid_tlv_len;
			capa->num_beacons =
				le32_to_cpup((const __le32 *)tlv_data);
			break;
		case IWL_UCODE_TLV_UMAC_DEBUG_ADDRS: {
			const struct iwl_umac_debug_addrs *dbg_ptrs =
				(const void *)tlv_data;

			if (tlv_len != sizeof(*dbg_ptrs))
				goto invalid_tlv_len;
			if (drv->trans->mac_cfg->device_family <
			    IWL_DEVICE_FAMILY_22000)
				break;
			drv->trans->dbg.umac_error_event_table =
				le32_to_cpu(dbg_ptrs->error_info_addr) &
				~FW_ADDR_CACHE_CONTROL;
			drv->trans->dbg.error_event_table_tlv_status |=
				IWL_ERROR_EVENT_TABLE_UMAC;
			break;
			}
		case IWL_UCODE_TLV_LMAC_DEBUG_ADDRS: {
			const struct iwl_lmac_debug_addrs *dbg_ptrs =
				(const void *)tlv_data;

			if (tlv_len != sizeof(*dbg_ptrs))
				goto invalid_tlv_len;
			if (drv->trans->mac_cfg->device_family <
			    IWL_DEVICE_FAMILY_22000)
				break;
			drv->trans->dbg.lmac_error_event_table[0] =
				le32_to_cpu(dbg_ptrs->error_event_table_ptr) &
				~FW_ADDR_CACHE_CONTROL;
			drv->trans->dbg.error_event_table_tlv_status |=
				IWL_ERROR_EVENT_TABLE_LMAC1;
			break;
			}
		case IWL_UCODE_TLV_TYPE_REGIONS:
			iwl_parse_dbg_tlv_assert_tables(drv, tlv);
			fallthrough;
		case IWL_UCODE_TLV_TYPE_DEBUG_INFO:
		case IWL_UCODE_TLV_TYPE_BUFFER_ALLOCATION:
		case IWL_UCODE_TLV_TYPE_HCMD:
		case IWL_UCODE_TLV_TYPE_TRIGGERS:
		case IWL_UCODE_TLV_TYPE_CONF_SET:
			if (iwlwifi_mod_params.enable_ini)
				iwl_dbg_tlv_alloc(drv->trans, tlv, false);
			break;
		case IWL_UCODE_TLV_CMD_VERSIONS:
			if (tlv_len % sizeof(struct iwl_fw_cmd_version)) {
				IWL_ERR(drv,
					"Invalid length for command versions: %u\n",
					tlv_len);
				tlv_len /= sizeof(struct iwl_fw_cmd_version);
				tlv_len *= sizeof(struct iwl_fw_cmd_version);
			}
			if (WARN_ON(capa->cmd_versions))
				return -EINVAL;
			capa->cmd_versions = kmemdup(tlv_data, tlv_len,
						     GFP_KERNEL);
			if (!capa->cmd_versions)
				return -ENOMEM;
			capa->n_cmd_versions =
				tlv_len / sizeof(struct iwl_fw_cmd_version);
			break;
		case IWL_UCODE_TLV_PHY_INTEGRATION_VERSION:
			if (drv->fw.phy_integration_ver) {
				IWL_ERR(drv,
					"phy integration str ignored, already exists\n");
				break;
			}

			drv->fw.phy_integration_ver =
				kmemdup(tlv_data, tlv_len, GFP_KERNEL);
			if (!drv->fw.phy_integration_ver)
				return -ENOMEM;
			drv->fw.phy_integration_ver_len = tlv_len;
			break;
		case IWL_UCODE_TLV_SEC_TABLE_ADDR:
		case IWL_UCODE_TLV_D3_KEK_KCK_ADDR:
			iwl_drv_set_dump_exclude(drv, tlv_type,
						 tlv_data, tlv_len);
			break;
		case IWL_UCODE_TLV_CURRENT_PC:
			if (tlv_len < sizeof(struct iwl_pc_data))
				goto invalid_tlv_len;
			drv->trans->dbg.pc_data =
				kmemdup(tlv_data, tlv_len, GFP_KERNEL);
			if (!drv->trans->dbg.pc_data)
				return -ENOMEM;
			drv->trans->dbg.num_pc =
				tlv_len / sizeof(struct iwl_pc_data);
			break;
		default:
			IWL_DEBUG_INFO(drv, "unknown TLV: %d\n", tlv_type);
			break;
		}
	}

	if (!fw_has_capa(capa, IWL_UCODE_TLV_CAPA_USNIFFER_UNIFIED) &&
	    usniffer_req && !*usniffer_images) {
		IWL_ERR(drv,
			"user selected to work with usniffer but usniffer image isn't available in ucode package\n");
		return -EINVAL;
	}

	if (len) {
		IWL_ERR(drv, "invalid TLV after parsing: %zd\n", len);
		iwl_print_hex_dump(drv, IWL_DL_FW, data, len);
		return -EINVAL;
	}

	return 0;

 invalid_tlv_len:
	IWL_ERR(drv, "TLV %d has invalid size: %u\n", tlv_type, tlv_len);
 tlv_error:
	iwl_print_hex_dump(drv, IWL_DL_FW, tlv_data, tlv_len);

	return -EINVAL;
}

static int iwl_alloc_ucode_mem(struct fw_img *out, struct fw_img_parsing *img)
{
	struct fw_desc *sec;

	sec = kcalloc(img->sec_counter, sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return -ENOMEM;

	out->sec = sec;
	out->num_sec = img->sec_counter;

	for (int i = 0; i < out->num_sec; i++)
		if (iwl_alloc_fw_desc(&sec[i], &img->sec[i]))
			return -ENOMEM;

	return 0;
}

static int iwl_alloc_ucode(struct iwl_drv *drv,
			   struct iwl_firmware_pieces *pieces,
			   enum iwl_ucode_type type)
{
	return iwl_alloc_ucode_mem(&drv->fw.img[type], &pieces->img[type]);
}

static int validate_sec_sizes(struct iwl_drv *drv,
			      struct iwl_firmware_pieces *pieces,
			      const struct iwl_cfg *cfg)
{
	IWL_DEBUG_INFO(drv, "f/w package hdr runtime inst size = %zd\n",
		get_sec_size(pieces, IWL_UCODE_REGULAR,
			     IWL_UCODE_SECTION_INST));
	IWL_DEBUG_INFO(drv, "f/w package hdr runtime data size = %zd\n",
		get_sec_size(pieces, IWL_UCODE_REGULAR,
			     IWL_UCODE_SECTION_DATA));
	IWL_DEBUG_INFO(drv, "f/w package hdr init inst size = %zd\n",
		get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST));
	IWL_DEBUG_INFO(drv, "f/w package hdr init data size = %zd\n",
		get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA));

	/* Verify that uCode images will fit in card's SRAM. */
	if (get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_INST) >
	    cfg->max_inst_size) {
		IWL_ERR(drv, "uCode instr len %zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_INST));
		return -1;
	}

	if (get_sec_size(pieces, IWL_UCODE_REGULAR, IWL_UCODE_SECTION_DATA) >
	    cfg->max_data_size) {
		IWL_ERR(drv, "uCode data len %zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_DATA));
		return -1;
	}

	if (get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_INST) >
	     cfg->max_inst_size) {
		IWL_ERR(drv, "uCode init instr len %zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_INIT,
				     IWL_UCODE_SECTION_INST));
		return -1;
	}

	if (get_sec_size(pieces, IWL_UCODE_INIT, IWL_UCODE_SECTION_DATA) >
	    cfg->max_data_size) {
		IWL_ERR(drv, "uCode init data len %zd too large to fit in\n",
			get_sec_size(pieces, IWL_UCODE_REGULAR,
				     IWL_UCODE_SECTION_DATA));
		return -1;
	}
	return 0;
}

static struct iwl_op_mode *
_iwl_op_mode_start(struct iwl_drv *drv, struct iwlwifi_opmode_table *op)
{
	const struct iwl_op_mode_ops *ops = op->ops;
	struct dentry *dbgfs_dir = NULL;
	struct iwl_op_mode *op_mode = NULL;
	int retry, max_retry = !!iwlwifi_mod_params.fw_restart * IWL_MAX_INIT_RETRY;

	/* also protects start/stop from racing against each other */
	lockdep_assert_held(&iwlwifi_opmode_table_mtx);

	for (retry = 0; retry <= max_retry; retry++) {

#ifdef CONFIG_IWLWIFI_DEBUGFS
		drv->dbgfs_op_mode = debugfs_create_dir(op->name,
							drv->dbgfs_drv);
		dbgfs_dir = drv->dbgfs_op_mode;
#endif

		op_mode = ops->start(drv->trans, drv->trans->cfg,
				     &drv->fw, dbgfs_dir);

		if (!IS_ERR(op_mode))
			return op_mode;

		if (test_bit(STATUS_TRANS_DEAD, &drv->trans->status))
			break;

#ifdef CONFIG_IWLWIFI_DEBUGFS
		debugfs_remove_recursive(drv->dbgfs_op_mode);
		drv->dbgfs_op_mode = NULL;
#endif

		if (PTR_ERR(op_mode) != -ETIMEDOUT)
			break;

		IWL_ERR(drv, "retry init count %d\n", retry);
	}

	return NULL;
}

static void _iwl_op_mode_stop(struct iwl_drv *drv)
{
	/* also protects start/stop from racing against each other */
	lockdep_assert_held(&iwlwifi_opmode_table_mtx);

	/* op_mode can be NULL if its start failed */
	if (drv->op_mode) {
		iwl_op_mode_stop(drv->op_mode);
		drv->op_mode = NULL;

#ifdef CONFIG_IWLWIFI_DEBUGFS
		debugfs_remove_recursive(drv->dbgfs_op_mode);
		drv->dbgfs_op_mode = NULL;
#endif
	}
}

#define IWL_MLD_SUPPORTED_FW_VERSION 97

/*
 * iwl_req_fw_callback - callback when firmware was loaded
 *
 * If loaded successfully, copies the firmware into buffers
 * for the card to fetch (via DMA).
 */
static void iwl_req_fw_callback(const struct firmware *ucode_raw, void *context)
{
	struct iwl_drv *drv = context;
	struct iwl_fw *fw = &drv->fw;
	const struct iwl_ucode_header *ucode;
	struct iwlwifi_opmode_table *op;
	int err;
	struct iwl_firmware_pieces *pieces;
	const unsigned int api_max = drv->trans->cfg->ucode_api_max;
	const unsigned int api_min = drv->trans->cfg->ucode_api_min;
	size_t trigger_tlv_sz[FW_DBG_TRIGGER_MAX];
	u32 api_ver;
	int i;
	bool usniffer_images = false;
	bool failure = true;

	fw->ucode_capa.max_probe_length = IWL_DEFAULT_MAX_PROBE_LENGTH;
	fw->ucode_capa.standard_phy_calibration_size =
			IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE;
	fw->ucode_capa.n_scan_channels = IWL_DEFAULT_SCAN_CHANNELS;
	fw->ucode_capa.num_stations = IWL_STATION_COUNT_MAX;
	fw->ucode_capa.num_beacons = 1;
	/* dump all fw memory areas by default */
	fw->dbg.dump_mask = 0xffffffff;

	pieces = kzalloc(sizeof(*pieces), GFP_KERNEL);
	if (!pieces)
		goto out_free_fw;

	if (!ucode_raw)
		goto try_again;

	IWL_DEBUG_FW_INFO(drv, "Loaded firmware file '%s' (%zd bytes).\n",
			  drv->firmware_name, ucode_raw->size);

	/* Make sure that we got at least the API version number */
	if (ucode_raw->size < 4) {
		IWL_ERR(drv, "File size way too small!\n");
		goto try_again;
	}

	/* Data from ucode file:  header followed by uCode images */
	ucode = (const struct iwl_ucode_header *)ucode_raw->data;

	if (ucode->ver)
		err = iwl_parse_v1_v2_firmware(drv, ucode_raw, pieces);
	else
		err = iwl_parse_tlv_firmware(drv, ucode_raw, pieces,
					     &fw->ucode_capa, &usniffer_images);

	if (err)
		goto try_again;

	if (fw_has_api(&drv->fw.ucode_capa, IWL_UCODE_TLV_API_NEW_VERSION))
		api_ver = drv->fw.ucode_ver;
	else
		api_ver = IWL_UCODE_API(drv->fw.ucode_ver);

	/*
	 * api_ver should match the api version forming part of the
	 * firmware filename ... but we don't check for that and only rely
	 * on the API version read from firmware header from here on forward
	 */
	if (api_ver < api_min || api_ver > api_max) {
		IWL_ERR(drv,
			"Driver unable to support your firmware API. "
			"Driver supports v%u, firmware is v%u.\n",
			api_max, api_ver);
		goto try_again;
	}

	/*
	 * In mvm uCode there is no difference between data and instructions
	 * sections.
	 */
	if (fw->type == IWL_FW_DVM && validate_sec_sizes(drv, pieces,
							 drv->trans->cfg))
		goto try_again;

	/* Allocate ucode buffers for card's bus-master loading ... */

	/* Runtime instructions and 2 copies of data:
	 * 1) unmodified from disk
	 * 2) backup cache for save/restore during power-downs
	 */
	for (i = 0; i < IWL_UCODE_TYPE_MAX; i++)
		if (iwl_alloc_ucode(drv, pieces, i))
			goto out_free_fw;

	if (pieces->dbg_dest_tlv_init) {
		size_t dbg_dest_size = sizeof(*drv->fw.dbg.dest_tlv) +
			sizeof(drv->fw.dbg.dest_tlv->reg_ops[0]) *
			drv->fw.dbg.n_dest_reg;

		drv->fw.dbg.dest_tlv = kmalloc(dbg_dest_size, GFP_KERNEL);

		if (!drv->fw.dbg.dest_tlv)
			goto out_free_fw;

		if (*pieces->dbg_dest_ver == 0) {
			memcpy(drv->fw.dbg.dest_tlv, pieces->dbg_dest_tlv_v1,
			       dbg_dest_size);
		} else {
			struct iwl_fw_dbg_dest_tlv_v1 *dest_tlv =
				drv->fw.dbg.dest_tlv;

			dest_tlv->version = pieces->dbg_dest_tlv->version;
			dest_tlv->monitor_mode =
				pieces->dbg_dest_tlv->monitor_mode;
			dest_tlv->size_power =
				pieces->dbg_dest_tlv->size_power;
			dest_tlv->wrap_count =
				pieces->dbg_dest_tlv->wrap_count;
			dest_tlv->write_ptr_reg =
				pieces->dbg_dest_tlv->write_ptr_reg;
			dest_tlv->base_shift =
				pieces->dbg_dest_tlv->base_shift;
			memcpy(dest_tlv->reg_ops,
			       pieces->dbg_dest_tlv->reg_ops,
			       sizeof(drv->fw.dbg.dest_tlv->reg_ops[0]) *
			       drv->fw.dbg.n_dest_reg);

			/* In version 1 of the destination tlv, which is
			 * relevant for internal buffer exclusively,
			 * the base address is part of given with the length
			 * of the buffer, and the size shift is give instead of
			 * end shift. We now store these values in base_reg,
			 * and end shift, and when dumping the data we'll
			 * manipulate it for extracting both the length and
			 * base address */
			dest_tlv->base_reg = pieces->dbg_dest_tlv->cfg_reg;
			dest_tlv->end_shift =
				pieces->dbg_dest_tlv->size_shift;
		}
	}

	for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.conf_tlv); i++) {
		if (pieces->dbg_conf_tlv[i]) {
			drv->fw.dbg.conf_tlv[i] =
				kmemdup(pieces->dbg_conf_tlv[i],
					pieces->dbg_conf_tlv_len[i],
					GFP_KERNEL);
			if (!drv->fw.dbg.conf_tlv[i])
				goto out_free_fw;
		}
	}

	memset(&trigger_tlv_sz, 0xff, sizeof(trigger_tlv_sz));

	trigger_tlv_sz[FW_DBG_TRIGGER_MISSED_BEACONS] =
		sizeof(struct iwl_fw_dbg_trigger_missed_bcon);
	trigger_tlv_sz[FW_DBG_TRIGGER_CHANNEL_SWITCH] = 0;
	trigger_tlv_sz[FW_DBG_TRIGGER_FW_NOTIF] =
		sizeof(struct iwl_fw_dbg_trigger_cmd);
	trigger_tlv_sz[FW_DBG_TRIGGER_MLME] =
		sizeof(struct iwl_fw_dbg_trigger_mlme);
	trigger_tlv_sz[FW_DBG_TRIGGER_STATS] =
		sizeof(struct iwl_fw_dbg_trigger_stats);
	trigger_tlv_sz[FW_DBG_TRIGGER_RSSI] =
		sizeof(struct iwl_fw_dbg_trigger_low_rssi);
	trigger_tlv_sz[FW_DBG_TRIGGER_TXQ_TIMERS] =
		sizeof(struct iwl_fw_dbg_trigger_txq_timer);
	trigger_tlv_sz[FW_DBG_TRIGGER_TIME_EVENT] =
		sizeof(struct iwl_fw_dbg_trigger_time_event);
	trigger_tlv_sz[FW_DBG_TRIGGER_BA] =
		sizeof(struct iwl_fw_dbg_trigger_ba);
	trigger_tlv_sz[FW_DBG_TRIGGER_TDLS] =
		sizeof(struct iwl_fw_dbg_trigger_tdls);

	for (i = 0; i < ARRAY_SIZE(drv->fw.dbg.trigger_tlv); i++) {
		if (pieces->dbg_trigger_tlv[i]) {
			/*
			 * If the trigger isn't long enough, WARN and exit.
			 * Someone is trying to debug something and he won't
			 * be able to catch the bug he is trying to chase.
			 * We'd better be noisy to be sure he knows what's
			 * going on.
			 */
			if (WARN_ON(pieces->dbg_trigger_tlv_len[i] <
				    (trigger_tlv_sz[i] +
				     sizeof(struct iwl_fw_dbg_trigger_tlv))))
				goto out_free_fw;
			drv->fw.dbg.trigger_tlv_len[i] =
				pieces->dbg_trigger_tlv_len[i];
			drv->fw.dbg.trigger_tlv[i] =
				kmemdup(pieces->dbg_trigger_tlv[i],
					drv->fw.dbg.trigger_tlv_len[i],
					GFP_KERNEL);
			if (!drv->fw.dbg.trigger_tlv[i])
				goto out_free_fw;
		}
	}

	/* Now that we can no longer fail, copy information */

	drv->fw.dbg.mem_tlv = pieces->dbg_mem_tlv;
	pieces->dbg_mem_tlv = NULL;
	drv->fw.dbg.n_mem_tlv = pieces->n_mem_tlv;

	/*
	 * The (size - 16) / 12 formula is based on the information recorded
	 * for each event, which is of mode 1 (including timestamp) for all
	 * new microcodes that include this information.
	 */
	fw->init_evtlog_ptr = pieces->init_evtlog_ptr;
	if (pieces->init_evtlog_size)
		fw->init_evtlog_size = (pieces->init_evtlog_size - 16)/12;
	else
		fw->init_evtlog_size =
			drv->trans->mac_cfg->base_params->max_event_log_size;
	fw->init_errlog_ptr = pieces->init_errlog_ptr;
	fw->inst_evtlog_ptr = pieces->inst_evtlog_ptr;
	if (pieces->inst_evtlog_size)
		fw->inst_evtlog_size = (pieces->inst_evtlog_size - 16)/12;
	else
		fw->inst_evtlog_size =
			drv->trans->mac_cfg->base_params->max_event_log_size;
	fw->inst_errlog_ptr = pieces->inst_errlog_ptr;

	/*
	 * figure out the offset of chain noise reset and gain commands
	 * base on the size of standard phy calibration commands table size
	 */
	if (fw->ucode_capa.standard_phy_calibration_size >
	    IWL_MAX_PHY_CALIBRATE_TBL_SIZE)
		fw->ucode_capa.standard_phy_calibration_size =
			IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE;

	/* We have our copies now, allow OS release its copies */
	release_firmware(ucode_raw);

	iwl_dbg_tlv_load_bin(drv->trans->dev, drv->trans);

	mutex_lock(&iwlwifi_opmode_table_mtx);
	switch (fw->type) {
	case IWL_FW_DVM:
		op = &iwlwifi_opmode_table[DVM_OP_MODE];
		break;
	default:
		WARN(1, "Invalid fw type %d\n", fw->type);
		fallthrough;
	case IWL_FW_MVM:
		op = &iwlwifi_opmode_table[MVM_OP_MODE];
		break;
	}

#if IS_ENABLED(CONFIG_IWLMLD)
	if (pieces->major >= IWL_MLD_SUPPORTED_FW_VERSION &&
	    iwl_drv_is_wifi7_supported(drv->trans))
		op = &iwlwifi_opmode_table[MLD_OP_MODE];
#else
	if (pieces->major >= IWL_MLD_SUPPORTED_FW_VERSION &&
	    iwl_drv_is_wifi7_supported(drv->trans)) {
		IWL_ERR(drv,
			"IWLMLD needs to be compiled to support this firmware\n");
		mutex_unlock(&iwlwifi_opmode_table_mtx);
		goto out_unbind;
	}
#endif

	IWL_INFO(drv, "loaded firmware version %s op_mode %s\n",
		 drv->fw.fw_version, op->name);

	/* add this device to the list of devices using this op_mode */
	list_add_tail(&drv->list, &op->drv);

	if (op->ops) {
		drv->op_mode = _iwl_op_mode_start(drv, op);

		if (!drv->op_mode) {
			mutex_unlock(&iwlwifi_opmode_table_mtx);
			goto out_unbind;
		}
	} else {
		request_module_nowait("%s", op->name);
	}
	mutex_unlock(&iwlwifi_opmode_table_mtx);

	complete(&drv->request_firmware_complete);

	failure = false;
	goto free;

 try_again:
	/* try next, if any */
	release_firmware(ucode_raw);
	if (iwl_request_firmware(drv, false))
		goto out_unbind;
	goto free;

 out_free_fw:
	release_firmware(ucode_raw);
 out_unbind:
	complete(&drv->request_firmware_complete);
	device_release_driver(drv->trans->dev);
	/* drv has just been freed by the release */
	failure = false;
 free:
	if (failure)
		iwl_dealloc_ucode(drv);

	if (pieces) {
		for (i = 0; i < ARRAY_SIZE(pieces->img); i++)
			kfree(pieces->img[i].sec);
		kfree(pieces->dbg_mem_tlv);
		kfree(pieces);
	}
}

struct iwl_drv *iwl_drv_start(struct iwl_trans *trans)
{
	struct iwl_drv *drv;
	int ret;

	drv = kzalloc(sizeof(*drv), GFP_KERNEL);
	if (!drv) {
		ret = -ENOMEM;
		goto err;
	}

	drv->trans = trans;
	drv->dev = trans->dev;

	init_completion(&drv->request_firmware_complete);
	INIT_LIST_HEAD(&drv->list);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	/* Create the device debugfs entries. */
	drv->dbgfs_drv = debugfs_create_dir(dev_name(trans->dev),
					    iwl_dbgfs_root);

	/* Create transport layer debugfs dir */
	drv->trans->dbgfs_dir = debugfs_create_dir("trans", drv->dbgfs_drv);
#endif

	drv->trans->dbg.domains_bitmap = IWL_TRANS_FW_DBG_DOMAIN(drv->trans);
	if (iwlwifi_mod_params.enable_ini != ENABLE_INI) {
		/* We have a non-default value in the module parameter,
		 * take its value
		 */
		drv->trans->dbg.domains_bitmap &= 0xffff;
		if (iwlwifi_mod_params.enable_ini != IWL_FW_INI_PRESET_DISABLE) {
			if (iwlwifi_mod_params.enable_ini > ENABLE_INI) {
				IWL_ERR(trans,
					"invalid enable_ini module parameter value: max = %d, using 0 instead\n",
					ENABLE_INI);
				iwlwifi_mod_params.enable_ini = 0;
			}
			drv->trans->dbg.domains_bitmap =
				BIT(IWL_FW_DBG_DOMAIN_POS + iwlwifi_mod_params.enable_ini);
		}
	}

	ret = iwl_request_firmware(drv, true);
	if (ret) {
		IWL_ERR(trans, "Couldn't request the fw\n");
		goto err_fw;
	}

	return drv;

err_fw:
#ifdef CONFIG_IWLWIFI_DEBUGFS
	debugfs_remove_recursive(drv->dbgfs_drv);
#endif
	iwl_dbg_tlv_free(drv->trans);
	kfree(drv);
err:
	return ERR_PTR(ret);
}

void iwl_drv_stop(struct iwl_drv *drv)
{
	wait_for_completion(&drv->request_firmware_complete);

	mutex_lock(&iwlwifi_opmode_table_mtx);

	_iwl_op_mode_stop(drv);

	iwl_dealloc_ucode(drv);

	/*
	 * List is empty (this item wasn't added)
	 * when firmware loading failed -- in that
	 * case we can't remove it from any list.
	 */
	if (!list_empty(&drv->list))
		list_del(&drv->list);
	mutex_unlock(&iwlwifi_opmode_table_mtx);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	iwl_trans_debugfs_cleanup(drv->trans);

	debugfs_remove_recursive(drv->dbgfs_drv);
#endif

	iwl_dbg_tlv_free(drv->trans);

	kfree(drv);
}

/* shared module parameters */
struct iwl_mod_params iwlwifi_mod_params = {
	.fw_restart = true,
	.bt_coex_active = true,
	.power_level = IWL_POWER_INDEX_1,
	.uapsd_disable = IWL_DISABLE_UAPSD_BSS | IWL_DISABLE_UAPSD_P2P_CLIENT,
	.enable_ini = ENABLE_INI,
	/* the rest are 0 by default */
};
IWL_EXPORT_SYMBOL(iwlwifi_mod_params);

int iwl_opmode_register(const char *name, const struct iwl_op_mode_ops *ops)
{
	int i;
	struct iwl_drv *drv;
	struct iwlwifi_opmode_table *op;

	mutex_lock(&iwlwifi_opmode_table_mtx);
	for (i = 0; i < ARRAY_SIZE(iwlwifi_opmode_table); i++) {
		op = &iwlwifi_opmode_table[i];
		if (strcmp(op->name, name))
			continue;
		op->ops = ops;
		/* TODO: need to handle exceptional case */
		list_for_each_entry(drv, &op->drv, list)
			drv->op_mode = _iwl_op_mode_start(drv, op);

		mutex_unlock(&iwlwifi_opmode_table_mtx);
		return 0;
	}
	mutex_unlock(&iwlwifi_opmode_table_mtx);
	return -EIO;
}
IWL_EXPORT_SYMBOL(iwl_opmode_register);

void iwl_opmode_deregister(const char *name)
{
	int i;
	struct iwl_drv *drv;

	mutex_lock(&iwlwifi_opmode_table_mtx);
	for (i = 0; i < ARRAY_SIZE(iwlwifi_opmode_table); i++) {
		if (strcmp(iwlwifi_opmode_table[i].name, name))
			continue;
		iwlwifi_opmode_table[i].ops = NULL;

		/* call the stop routine for all devices */
		list_for_each_entry(drv, &iwlwifi_opmode_table[i].drv, list)
			_iwl_op_mode_stop(drv);

		mutex_unlock(&iwlwifi_opmode_table_mtx);
		return;
	}
	mutex_unlock(&iwlwifi_opmode_table_mtx);
}
IWL_EXPORT_SYMBOL(iwl_opmode_deregister);

static int __init iwl_drv_init(void)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(iwlwifi_opmode_table); i++)
		INIT_LIST_HEAD(&iwlwifi_opmode_table[i].drv);

	pr_info(DRV_DESCRIPTION "\n");

#ifdef CONFIG_IWLWIFI_DEBUGFS
	/* Create the root of iwlwifi debugfs subsystem. */
	iwl_dbgfs_root = debugfs_create_dir(DRV_NAME, NULL);
#endif

	err = iwl_pci_register_driver();
	if (err)
		goto cleanup_debugfs;

	return 0;

cleanup_debugfs:
#ifdef CONFIG_IWLWIFI_DEBUGFS
	debugfs_remove_recursive(iwl_dbgfs_root);
#endif
	return err;
}
module_init(iwl_drv_init);

static void __exit iwl_drv_exit(void)
{
	iwl_pci_unregister_driver();
	iwl_trans_free_restart_list();

#ifdef CONFIG_IWLWIFI_DEBUGFS
	debugfs_remove_recursive(iwl_dbgfs_root);
#endif
}
module_exit(iwl_drv_exit);

#ifdef CONFIG_IWLWIFI_DEBUG
module_param_named(debug, iwlwifi_mod_params.debug_level, uint, 0644);
MODULE_PARM_DESC(debug, "debug output mask");
#endif

module_param_named(swcrypto, iwlwifi_mod_params.swcrypto, int, 0444);
MODULE_PARM_DESC(swcrypto, "using crypto in software (default 0 [hardware])");
module_param_named(11n_disable, iwlwifi_mod_params.disable_11n, uint, 0444);
MODULE_PARM_DESC(11n_disable,
	"disable 11n functionality, bitmap: 1: full, 2: disable agg TX, 4: disable agg RX, 8 enable agg TX");
module_param_named(amsdu_size, iwlwifi_mod_params.amsdu_size, int, 0444);
MODULE_PARM_DESC(amsdu_size,
		 "amsdu size 0: 12K for multi Rx queue devices, 2K for AX210 devices, "
		 "4K for other devices 1:4K 2:8K 3:12K (16K buffers) 4: 2K (default 0)");
module_param_named(fw_restart, iwlwifi_mod_params.fw_restart, bool, 0444);
MODULE_PARM_DESC(fw_restart, "restart firmware in case of error (default true)");

module_param_named(nvm_file, iwlwifi_mod_params.nvm_file, charp, 0444);
MODULE_PARM_DESC(nvm_file, "NVM file name");

module_param_named(uapsd_disable, iwlwifi_mod_params.uapsd_disable, uint, 0644);
MODULE_PARM_DESC(uapsd_disable,
		 "disable U-APSD functionality bitmap 1: BSS 2: P2P Client (default: 3)");

module_param_named(enable_ini, iwlwifi_mod_params.enable_ini, uint, 0444);
MODULE_PARM_DESC(enable_ini,
		 "0:disable, 1-15:FW_DBG_PRESET Values, 16:enabled without preset value defined,"
		 "Debug INI TLV FW debug infrastructure (default: 16)");

/*
 * set bt_coex_active to true, uCode will do kill/defer
 * every time the priority line is asserted (BT is sending signals on the
 * priority line in the PCIx).
 * set bt_coex_active to false, uCode will ignore the BT activity and
 * perform the normal operation
 *
 * User might experience transmit issue on some platform due to WiFi/BT
 * co-exist problem. The possible behaviors are:
 *   Able to scan and finding all the available AP
 *   Not able to associate with any AP
 * On those platforms, WiFi communication can be restored by set
 * "bt_coex_active" module parameter to "false"
 *
 * default: bt_coex_active = true (BT_COEX_ENABLE)
 */
module_param_named(bt_coex_active, iwlwifi_mod_params.bt_coex_active,
		   bool, 0444);
MODULE_PARM_DESC(bt_coex_active, "enable wifi/bt co-exist (default: enable)");

module_param_named(led_mode, iwlwifi_mod_params.led_mode, int, 0444);
MODULE_PARM_DESC(led_mode, "0=system default, "
		"1=On(RF On)/Off(RF Off), 2=blinking, 3=Off (default: 0)");

module_param_named(power_save, iwlwifi_mod_params.power_save, bool, 0444);
MODULE_PARM_DESC(power_save,
		 "enable WiFi power management (default: disable)");

module_param_named(power_level, iwlwifi_mod_params.power_level, int, 0444);
MODULE_PARM_DESC(power_level,
		 "default power save level (range from 1 - 5, default: 1)");

module_param_named(disable_11ac, iwlwifi_mod_params.disable_11ac, bool, 0444);
MODULE_PARM_DESC(disable_11ac, "Disable VHT capabilities (default: false)");

module_param_named(remove_when_gone,
		   iwlwifi_mod_params.remove_when_gone, bool,
		   0444);
MODULE_PARM_DESC(remove_when_gone,
		 "Remove dev from PCIe bus if it is deemed inaccessible (default: false)");

module_param_named(disable_11ax, iwlwifi_mod_params.disable_11ax, bool,
		   S_IRUGO);
MODULE_PARM_DESC(disable_11ax, "Disable HE capabilities (default: false)");

module_param_named(disable_11be, iwlwifi_mod_params.disable_11be, bool, 0444);
MODULE_PARM_DESC(disable_11be, "Disable EHT capabilities (default: false)");
