// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2025 Intel Corporation */
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/types.h>

#include <adf_accel_devices.h>
#include <adf_admin.h>
#include <adf_bank_state.h>
#include <adf_cfg.h>
#include <adf_cfg_services.h>
#include <adf_clock.h>
#include <adf_common_drv.h>
#include <adf_fw_config.h>
#include <adf_gen6_pm.h>
#include <adf_gen6_ras.h>
#include <adf_gen6_shared.h>
#include <adf_gen6_tl.h>
#include <adf_timer.h>
#include "adf_6xxx_hw_data.h"
#include "icp_qat_fw_comp.h"
#include "icp_qat_hw_51_comp.h"

#define RP_GROUP_0_MASK		(BIT(0) | BIT(2))
#define RP_GROUP_1_MASK		(BIT(1) | BIT(3))
#define RP_GROUP_ALL_MASK	(RP_GROUP_0_MASK | RP_GROUP_1_MASK)

#define ADF_AE_GROUP_0		GENMASK(3, 0)
#define ADF_AE_GROUP_1		GENMASK(7, 4)
#define ADF_AE_GROUP_2		BIT(8)

struct adf_ring_config {
	u32 ring_mask;
	enum adf_cfg_service_type ring_type;
	const unsigned long *thrd_mask;
};

static u32 rmask_two_services[] = {
	RP_GROUP_0_MASK,
	RP_GROUP_1_MASK,
};

enum adf_gen6_rps {
	RP0 = 0,
	RP1 = 1,
	RP2 = 2,
	RP3 = 3,
	RP_MAX = RP3
};

/*
 * thrd_mask_[sym|asym|cpr|dcc]: these static arrays define the thread
 * configuration for handling requests of specific services across the
 * accelerator engines. Each element in an array corresponds to an
 * accelerator engine, with the value being a bitmask that specifies which
 * threads within that engine are capable of processing the particular service.
 *
 * For example, a value of 0x0C means that threads 2 and 3 are enabled for the
 * service in the respective accelerator engine.
 */
static const unsigned long thrd_mask_sym[ADF_6XXX_MAX_ACCELENGINES] = {
	0x0C, 0x0C, 0x0C, 0x0C, 0x1C, 0x1C, 0x1C, 0x1C, 0x00
};

static const unsigned long thrd_mask_asym[ADF_6XXX_MAX_ACCELENGINES] = {
	0x70, 0x70, 0x70, 0x70, 0x60, 0x60, 0x60, 0x60, 0x00
};

static const unsigned long thrd_mask_cpr[ADF_6XXX_MAX_ACCELENGINES] = {
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00
};

static const unsigned long thrd_mask_dcc[ADF_6XXX_MAX_ACCELENGINES] = {
	0x00, 0x00, 0x00, 0x00, 0x07, 0x07, 0x03, 0x03, 0x00
};

static const unsigned long thrd_mask_dcpr[ADF_6XXX_MAX_ACCELENGINES] = {
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00
};

static const char *const adf_6xxx_fw_objs[] = {
	[ADF_FW_CY_OBJ] = ADF_6XXX_CY_OBJ,
	[ADF_FW_DC_OBJ] = ADF_6XXX_DC_OBJ,
	[ADF_FW_ADMIN_OBJ] = ADF_6XXX_ADMIN_OBJ,
};

static const struct adf_fw_config adf_default_fw_config[] = {
	{ ADF_AE_GROUP_1, ADF_FW_DC_OBJ },
	{ ADF_AE_GROUP_0, ADF_FW_CY_OBJ },
	{ ADF_AE_GROUP_2, ADF_FW_ADMIN_OBJ },
};

static struct adf_hw_device_class adf_6xxx_class = {
	.name = ADF_6XXX_DEVICE_NAME,
	.type = DEV_6XXX,
};

static bool services_supported(unsigned long mask)
{
	int num_svc;

	if (mask >= BIT(SVC_COUNT))
		return false;

	num_svc = hweight_long(mask);
	switch (num_svc) {
	case ADF_ONE_SERVICE:
		return true;
	case ADF_TWO_SERVICES:
	case ADF_THREE_SERVICES:
		return !test_bit(SVC_DCC, &mask);
	default:
		return false;
	}
}

static int get_service(unsigned long *mask)
{
	if (test_and_clear_bit(SVC_ASYM, mask))
		return SVC_ASYM;

	if (test_and_clear_bit(SVC_SYM, mask))
		return SVC_SYM;

	if (test_and_clear_bit(SVC_DC, mask))
		return SVC_DC;

	if (test_and_clear_bit(SVC_DCC, mask))
		return SVC_DCC;

	if (test_and_clear_bit(SVC_DECOMP, mask))
		return SVC_DECOMP;

	return -EINVAL;
}

static enum adf_cfg_service_type get_ring_type(unsigned int service)
{
	switch (service) {
	case SVC_SYM:
		return SYM;
	case SVC_ASYM:
		return ASYM;
	case SVC_DC:
	case SVC_DCC:
		return COMP;
	case SVC_DECOMP:
		return DECOMP;
	default:
		return UNUSED;
	}
}

static const unsigned long *get_thrd_mask(unsigned int service)
{
	switch (service) {
	case SVC_SYM:
		return thrd_mask_sym;
	case SVC_ASYM:
		return thrd_mask_asym;
	case SVC_DC:
		return thrd_mask_cpr;
	case SVC_DCC:
		return thrd_mask_dcc;
	case SVC_DECOMP:
		return thrd_mask_dcpr;
	default:
		return NULL;
	}
}

static int get_rp_config(struct adf_accel_dev *accel_dev, struct adf_ring_config *rp_config,
			 unsigned int *num_services)
{
	unsigned int i, nservices;
	unsigned long mask;
	int ret, service;

	ret = adf_get_service_mask(accel_dev, &mask);
	if (ret)
		return ret;

	nservices = hweight_long(mask);
	if (nservices > MAX_NUM_CONCURR_SVC)
		return -EINVAL;

	for (i = 0; i < nservices; i++) {
		service = get_service(&mask);
		if (service < 0)
			return service;

		rp_config[i].ring_type = get_ring_type(service);
		rp_config[i].thrd_mask = get_thrd_mask(service);

		/*
		 * If there is only one service enabled, use all ring pairs for
		 * that service.
		 * If there are two services enabled, use ring pairs 0 and 2 for
		 * one service and ring pairs 1 and 3 for the other service.
		 */
		switch (nservices) {
		case ADF_ONE_SERVICE:
			rp_config[i].ring_mask = RP_GROUP_ALL_MASK;
			break;
		case ADF_TWO_SERVICES:
			rp_config[i].ring_mask = rmask_two_services[i];
			break;
		case ADF_THREE_SERVICES:
			rp_config[i].ring_mask = BIT(i);

			/* If ASYM is enabled, use additional ring pair */
			if (service == SVC_ASYM)
				rp_config[i].ring_mask |= BIT(RP3);

			break;
		default:
			return -EINVAL;
		}
	}

	*num_services = nservices;

	return 0;
}

static u32 adf_gen6_get_arb_mask(struct adf_accel_dev *accel_dev, unsigned int ae)
{
	struct adf_ring_config rp_config[MAX_NUM_CONCURR_SVC];
	unsigned int num_services, i, thrd;
	u32 ring_mask, thd2arb_mask = 0;
	const unsigned long *p_mask;

	if (get_rp_config(accel_dev, rp_config, &num_services))
		return 0;

	/*
	 * The thd2arb_mask maps ring pairs to threads within an accelerator engine.
	 * It ensures that jobs submitted to ring pairs are scheduled on threads capable
	 * of handling the specified service type.
	 *
	 * Each group of 4 bits in the mask corresponds to a thread, with each bit
	 * indicating whether a job from a ring pair can be scheduled on that thread.
	 * The use of 4 bits is due to the organization of ring pairs into groups of
	 * four, where each group shares the same configuration.
	 */
	for (i = 0; i < num_services; i++) {
		p_mask = &rp_config[i].thrd_mask[ae];
		ring_mask = rp_config[i].ring_mask;

		for_each_set_bit(thrd, p_mask, ADF_NUM_THREADS_PER_AE)
			thd2arb_mask |= ring_mask << (thrd * 4);
	}

	return thd2arb_mask;
}

static u16 get_ring_to_svc_map(struct adf_accel_dev *accel_dev)
{
	enum adf_cfg_service_type rps[ADF_GEN6_NUM_BANKS_PER_VF] = { };
	struct adf_ring_config rp_config[MAX_NUM_CONCURR_SVC];
	unsigned int num_services, rp_num, i;
	unsigned long cfg_mask;
	u16 ring_to_svc_map;

	if (get_rp_config(accel_dev, rp_config, &num_services))
		return 0;

	/*
	 * Loop through the configured services and populate the `rps` array that
	 * contains what service that particular ring pair can handle (i.e. symmetric
	 * crypto, asymmetric crypto, data compression or compression chaining).
	 */
	for (i = 0; i < num_services; i++) {
		cfg_mask = rp_config[i].ring_mask;
		for_each_set_bit(rp_num, &cfg_mask, ADF_GEN6_NUM_BANKS_PER_VF)
			rps[rp_num] = rp_config[i].ring_type;
	}

	/*
	 * The ring_mask is structured into segments of 3 bits, with each
	 * segment representing the service configuration for a specific ring pair.
	 * Since ring pairs are organized into groups of 4, the ring_mask contains 4
	 * such 3-bit segments, each corresponding to one ring pair.
	 *
	 * The device has 64 ring pairs, which are organized in groups of 4, namely
	 * 16 groups. Each group has the same configuration, represented here by
	 * `ring_to_svc_map`.
	 */
	ring_to_svc_map = rps[RP0] << ADF_CFG_SERV_RING_PAIR_0_SHIFT |
			  rps[RP1] << ADF_CFG_SERV_RING_PAIR_1_SHIFT |
			  rps[RP2] << ADF_CFG_SERV_RING_PAIR_2_SHIFT |
			  rps[RP3] << ADF_CFG_SERV_RING_PAIR_3_SHIFT;

	return ring_to_svc_map;
}

static u32 get_accel_mask(struct adf_hw_device_data *self)
{
	return ADF_GEN6_ACCELERATORS_MASK;
}

static u32 get_num_accels(struct adf_hw_device_data *self)
{
	return ADF_GEN6_MAX_ACCELERATORS;
}

static u32 get_num_aes(struct adf_hw_device_data *self)
{
	return self ? hweight32(self->ae_mask) : 0;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN6_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN6_ETR_BAR;
}

static u32 get_sram_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN6_SRAM_BAR;
}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	return DEV_SKU_1;
}

static void get_arb_info(struct arb_info *arb_info)
{
	arb_info->arb_cfg = ADF_GEN6_ARB_CONFIG;
	arb_info->arb_offset = ADF_GEN6_ARB_OFFSET;
	arb_info->wt2sam_offset = ADF_GEN6_ARB_WRK_2_SER_MAP_OFFSET;
}

static void get_admin_info(struct admin_info *admin_csrs_info)
{
	admin_csrs_info->mailbox_offset = ADF_GEN6_MAILBOX_BASE_OFFSET;
	admin_csrs_info->admin_msg_ur = ADF_GEN6_ADMINMSGUR_OFFSET;
	admin_csrs_info->admin_msg_lr = ADF_GEN6_ADMINMSGLR_OFFSET;
}

static u32 get_heartbeat_clock(struct adf_hw_device_data *self)
{
	return ADF_GEN6_COUNTER_FREQ;
}

static void enable_error_correction(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	/*
	 * Enable all error notification bits in errsou3 except VFLR
	 * notification on host.
	 */
	ADF_CSR_WR(csr, ADF_GEN6_ERRMSK3, ADF_GEN6_VFLNOTIFY);
}

static void enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr = adf_get_pmisc_base(accel_dev);

	/* Enable bundle interrupts */
	ADF_CSR_WR(addr, ADF_GEN6_SMIAPF_RP_X0_MASK_OFFSET, 0);
	ADF_CSR_WR(addr, ADF_GEN6_SMIAPF_RP_X1_MASK_OFFSET, 0);

	/* Enable misc interrupts */
	ADF_CSR_WR(addr, ADF_GEN6_SMIAPF_MASK_OFFSET, 0);
}

static void set_ssm_wdtimer(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr = adf_get_pmisc_base(accel_dev);
	u64 val_pke = ADF_SSM_WDT_PKE_DEFAULT_VALUE;
	u64 val = ADF_SSM_WDT_DEFAULT_VALUE;

	/* Enable watchdog timer for sym and dc */
	ADF_CSR_WR64_LO_HI(addr, ADF_SSMWDTATHL_OFFSET, ADF_SSMWDTATHH_OFFSET, val);
	ADF_CSR_WR64_LO_HI(addr, ADF_SSMWDTCNVL_OFFSET, ADF_SSMWDTCNVH_OFFSET, val);
	ADF_CSR_WR64_LO_HI(addr, ADF_SSMWDTUCSL_OFFSET, ADF_SSMWDTUCSH_OFFSET, val);
	ADF_CSR_WR64_LO_HI(addr, ADF_SSMWDTDCPRL_OFFSET, ADF_SSMWDTDCPRH_OFFSET, val);

	/* Enable watchdog timer for pke */
	ADF_CSR_WR64_LO_HI(addr, ADF_SSMWDTPKEL_OFFSET, ADF_SSMWDTPKEH_OFFSET, val_pke);
}

/*
 * The vector routing table is used to select the MSI-X entry to use for each
 * interrupt source.
 * The first ADF_GEN6_ETR_MAX_BANKS entries correspond to ring interrupts.
 * The final entry corresponds to VF2PF or error interrupts.
 * This vector table could be used to configure one MSI-X entry to be shared
 * between multiple interrupt sources.
 *
 * The default routing is set to have a one to one correspondence between the
 * interrupt source and the MSI-X entry used.
 */
static void set_msix_default_rttable(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	unsigned int i;

	for (i = 0; i <= ADF_GEN6_ETR_MAX_BANKS; i++)
		ADF_CSR_WR(csr, ADF_GEN6_MSIX_RTTABLE_OFFSET(i), i);
}

static int reset_ring_pair(void __iomem *csr, u32 bank_number)
{
	u32 status;
	int ret;

	/*
	 * Write rpresetctl register BIT(0) as 1.
	 * Since rpresetctl registers have no RW fields, no need to preserve
	 * values for other bits. Just write directly.
	 */
	ADF_CSR_WR(csr, ADF_WQM_CSR_RPRESETCTL(bank_number),
		   ADF_WQM_CSR_RPRESETCTL_RESET);

	/* Read rpresetsts register and wait for rp reset to complete */
	ret = read_poll_timeout(ADF_CSR_RD, status,
				status & ADF_WQM_CSR_RPRESETSTS_STATUS,
				ADF_RPRESET_POLL_DELAY_US,
				ADF_RPRESET_POLL_TIMEOUT_US, true,
				csr, ADF_WQM_CSR_RPRESETSTS(bank_number));
	if (ret)
		return ret;

	/* When ring pair reset is done, clear rpresetsts */
	ADF_CSR_WR(csr, ADF_WQM_CSR_RPRESETSTS(bank_number), ADF_WQM_CSR_RPRESETSTS_STATUS);

	return 0;
}

static int ring_pair_reset(struct adf_accel_dev *accel_dev, u32 bank_number)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *csr = adf_get_etr_base(accel_dev);
	int ret;

	if (bank_number >= hw_data->num_banks)
		return -EINVAL;

	dev_dbg(&GET_DEV(accel_dev), "ring pair reset for bank:%d\n", bank_number);

	ret = reset_ring_pair(csr, bank_number);
	if (ret)
		dev_err(&GET_DEV(accel_dev), "ring pair reset failed (timeout)\n");
	else
		dev_dbg(&GET_DEV(accel_dev), "ring pair reset successful\n");

	return ret;
}

static int build_comp_block(void *ctx, enum adf_dc_algo algo)
{
	struct icp_qat_fw_comp_req *req_tmpl = ctx;
	struct icp_qat_fw_comp_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_hw_comp_51_config_csr_lower hw_comp_lower_csr = { };
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	u32 lower_val;

	switch (algo) {
	case QAT_DEFLATE:
		header->service_cmd_id = ICP_QAT_FW_COMP_CMD_DYNAMIC;
	break;
	default:
		return -EINVAL;
	}

	hw_comp_lower_csr.lllbd = ICP_QAT_HW_COMP_51_LLLBD_CTRL_LLLBD_DISABLED;
	hw_comp_lower_csr.sd = ICP_QAT_HW_COMP_51_SEARCH_DEPTH_LEVEL_1;
	lower_val = ICP_QAT_FW_COMP_51_BUILD_CONFIG_LOWER(hw_comp_lower_csr);
	cd_pars->u.sl.comp_slice_cfg_word[0] = lower_val;
	cd_pars->u.sl.comp_slice_cfg_word[1] = 0;

	return 0;
}

static int build_decomp_block(void *ctx, enum adf_dc_algo algo)
{
	struct icp_qat_fw_comp_req *req_tmpl = ctx;
	struct icp_qat_fw_comp_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;

	switch (algo) {
	case QAT_DEFLATE:
		header->service_cmd_id = ICP_QAT_FW_COMP_CMD_DECOMPRESS;
	break;
	default:
		return -EINVAL;
	}

	cd_pars->u.sl.comp_slice_cfg_word[0] = 0;
	cd_pars->u.sl.comp_slice_cfg_word[1] = 0;

	return 0;
}

static void adf_gen6_init_dc_ops(struct adf_dc_ops *dc_ops)
{
	dc_ops->build_comp_block = build_comp_block;
	dc_ops->build_decomp_block = build_decomp_block;
}

static int adf_gen6_init_thd2arb_map(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	u32 *thd2arb_map = hw_data->thd_to_arb_map;
	unsigned int i;

	for (i = 0; i < hw_data->num_engines; i++) {
		thd2arb_map[i] = adf_gen6_get_arb_mask(accel_dev, i);
		dev_dbg(&GET_DEV(accel_dev), "ME:%d arb_mask:%#x\n", i, thd2arb_map[i]);
	}

	return 0;
}

static void init_num_svc_aes(struct adf_rl_hw_data *device_data)
{
	enum adf_fw_objs obj_type, obj_iter;
	unsigned int svc, i, num_grp;
	u32 ae_mask;

	for (svc = 0; svc < SVC_BASE_COUNT; svc++) {
		switch (svc) {
		case SVC_SYM:
		case SVC_ASYM:
			obj_type = ADF_FW_CY_OBJ;
			break;
		case SVC_DC:
		case SVC_DECOMP:
			obj_type = ADF_FW_DC_OBJ;
			break;
		}

		num_grp = ARRAY_SIZE(adf_default_fw_config);
		for (i = 0; i < num_grp; i++) {
			obj_iter = adf_default_fw_config[i].obj;
			if (obj_iter == obj_type) {
				ae_mask = adf_default_fw_config[i].ae_mask;
				device_data->svc_ae_mask[svc] = hweight32(ae_mask);
				break;
			}
		}
	}
}

static u32 adf_gen6_get_svc_slice_cnt(struct adf_accel_dev *accel_dev,
				      enum adf_base_services svc)
{
	struct adf_rl_hw_data *device_data = &accel_dev->hw_device->rl_data;

	switch (svc) {
	case SVC_SYM:
		return device_data->slices.cph_cnt;
	case SVC_ASYM:
		return device_data->slices.pke_cnt;
	case SVC_DC:
		return device_data->slices.cpr_cnt + device_data->slices.dcpr_cnt;
	case SVC_DECOMP:
		return device_data->slices.dcpr_cnt;
	default:
		return 0;
	}
}

static void set_vc_csr_for_bank(void __iomem *csr, u32 bank_number)
{
	u32 value;

	/*
	 * After each PF FLR, for each of the 64 ring pairs in the PF, the
	 * driver must program the ringmodectl CSRs.
	 */
	value = ADF_CSR_RD(csr, ADF_GEN6_CSR_RINGMODECTL(bank_number));
	FIELD_MODIFY(ADF_GEN6_RINGMODECTL_TC_MASK, &value, ADF_GEN6_RINGMODECTL_TC_DEFAULT);
	FIELD_MODIFY(ADF_GEN6_RINGMODECTL_TC_EN_MASK, &value, ADF_GEN6_RINGMODECTL_TC_EN_OP1);
	ADF_CSR_WR(csr, ADF_GEN6_CSR_RINGMODECTL(bank_number), value);
}

static int set_vc_config(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	u32 value;
	int err;

	/*
	 * After each PF FLR, the driver must program the Port Virtual Channel (VC)
	 * Control Registers.
	 * Read PVC0CTL then write the masked values.
	 */
	pci_read_config_dword(pdev, ADF_GEN6_PVC0CTL_OFFSET, &value);
	FIELD_MODIFY(ADF_GEN6_PVC0CTL_TCVCMAP_MASK, &value, ADF_GEN6_PVC0CTL_TCVCMAP_DEFAULT);
	err = pci_write_config_dword(pdev, ADF_GEN6_PVC0CTL_OFFSET, value);
	if (err) {
		dev_err(&GET_DEV(accel_dev), "pci write to PVC0CTL failed\n");
		return pcibios_err_to_errno(err);
	}

	/* Read PVC1CTL then write masked values */
	pci_read_config_dword(pdev, ADF_GEN6_PVC1CTL_OFFSET, &value);
	FIELD_MODIFY(ADF_GEN6_PVC1CTL_TCVCMAP_MASK, &value, ADF_GEN6_PVC1CTL_TCVCMAP_DEFAULT);
	FIELD_MODIFY(ADF_GEN6_PVC1CTL_VCEN_MASK, &value, ADF_GEN6_PVC1CTL_VCEN_ON);
	err = pci_write_config_dword(pdev, ADF_GEN6_PVC1CTL_OFFSET, value);
	if (err)
		dev_err(&GET_DEV(accel_dev), "pci write to PVC1CTL failed\n");

	return pcibios_err_to_errno(err);
}

static int adf_gen6_set_vc(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	void __iomem *csr = adf_get_etr_base(accel_dev);
	u32 i;

	for (i = 0; i < hw_data->num_banks; i++) {
		dev_dbg(&GET_DEV(accel_dev), "set virtual channels for bank:%d\n", i);
		set_vc_csr_for_bank(csr, i);
	}

	return set_vc_config(accel_dev);
}

static u32 get_ae_mask(struct adf_hw_device_data *self)
{
	unsigned long fuses = self->fuses[ADF_FUSECTL4];
	u32 mask = ADF_6XXX_ACCELENGINES_MASK;

	/*
	 * If bit 0 is set in the fuses, the first 4 engines are disabled.
	 * If bit 4 is set, the second group of 4 engines are disabled.
	 * If bit 8 is set, the admin engine (bit 8) is disabled.
	 */
	if (test_bit(0, &fuses))
		mask &= ~ADF_AE_GROUP_0;

	if (test_bit(4, &fuses))
		mask &= ~ADF_AE_GROUP_1;

	if (test_bit(8, &fuses))
		mask &= ~ADF_AE_GROUP_2;

	return mask;
}

static u32 get_accel_cap(struct adf_accel_dev *accel_dev)
{
	u32 capabilities_sym, capabilities_asym;
	u32 capabilities_dc;
	unsigned long mask;
	u32 caps = 0;
	u32 fusectl1;

	fusectl1 = GET_HW_DATA(accel_dev)->fuses[ADF_FUSECTL1];

	/* Read accelerator capabilities mask */
	capabilities_sym = ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC |
			  ICP_ACCEL_CAPABILITIES_CIPHER |
			  ICP_ACCEL_CAPABILITIES_AUTHENTICATION |
			  ICP_ACCEL_CAPABILITIES_SHA3 |
			  ICP_ACCEL_CAPABILITIES_SHA3_EXT |
			  ICP_ACCEL_CAPABILITIES_CHACHA_POLY |
			  ICP_ACCEL_CAPABILITIES_AESGCM_SPC |
			  ICP_ACCEL_CAPABILITIES_AES_V2;

	/* A set bit in fusectl1 means the corresponding feature is OFF in this SKU */
	if (fusectl1 & ICP_ACCEL_GEN6_MASK_UCS_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CHACHA_POLY;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_AESGCM_SPC;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_AES_V2;
	}
	if (fusectl1 & ICP_ACCEL_GEN6_MASK_AUTH_SLICE) {
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_AUTHENTICATION;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_SHA3;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_SHA3_EXT;
		capabilities_sym &= ~ICP_ACCEL_CAPABILITIES_CIPHER;
	}

	capabilities_asym = ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
			    ICP_ACCEL_CAPABILITIES_SM2 |
			    ICP_ACCEL_CAPABILITIES_ECEDMONT;

	if (fusectl1 & ICP_ACCEL_GEN6_MASK_PKE_SLICE) {
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_SM2;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_ECEDMONT;
	}

	capabilities_dc = ICP_ACCEL_CAPABILITIES_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;

	if (fusectl1 & ICP_ACCEL_GEN6_MASK_CPR_SLICE) {
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;
	}

	if (adf_get_service_mask(accel_dev, &mask))
		return 0;

	if (test_bit(SVC_ASYM, &mask))
		caps |= capabilities_asym;
	if (test_bit(SVC_SYM, &mask))
		caps |= capabilities_sym;
	if (test_bit(SVC_DC, &mask) || test_bit(SVC_DECOMP, &mask))
		caps |= capabilities_dc;
	if (test_bit(SVC_DCC, &mask)) {
		/*
		 * Sym capabilities are available for chaining operations,
		 * but sym crypto instances cannot be supported
		 */
		caps = capabilities_dc | capabilities_sym;
		caps &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
	}

	return caps;
}

static u32 uof_get_num_objs(struct adf_accel_dev *accel_dev)
{
	return ARRAY_SIZE(adf_default_fw_config);
}

static const char *uof_get_name(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	int num_fw_objs = ARRAY_SIZE(adf_6xxx_fw_objs);
	int id;

	id = adf_default_fw_config[obj_num].obj;
	if (id >= num_fw_objs)
		return NULL;

	return adf_6xxx_fw_objs[id];
}

static const char *uof_get_name_6xxx(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	return uof_get_name(accel_dev, obj_num);
}

static int uof_get_obj_type(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	if (obj_num >= uof_get_num_objs(accel_dev))
		return -EINVAL;

	return adf_default_fw_config[obj_num].obj;
}

static u32 uof_get_ae_mask(struct adf_accel_dev *accel_dev, u32 obj_num)
{
	return adf_default_fw_config[obj_num].ae_mask;
}

static const u32 *adf_get_arbiter_mapping(struct adf_accel_dev *accel_dev)
{
	if (adf_gen6_init_thd2arb_map(accel_dev))
		dev_warn(&GET_DEV(accel_dev),
			 "Failed to generate thread to arbiter mapping");

	return GET_HW_DATA(accel_dev)->thd_to_arb_map;
}

static int adf_init_device(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr = adf_get_pmisc_base(accel_dev);
	u32 status;
	u32 csr;
	int ret;

	/* Temporarily mask PM interrupt */
	csr = ADF_CSR_RD(addr, ADF_GEN6_ERRMSK2);
	csr |= ADF_GEN6_PM_SOU;
	ADF_CSR_WR(addr, ADF_GEN6_ERRMSK2, csr);

	/* Set DRV_ACTIVE bit to power up the device */
	ADF_CSR_WR(addr, ADF_GEN6_PM_INTERRUPT, ADF_GEN6_PM_DRV_ACTIVE);

	/* Poll status register to make sure the device is powered up */
	ret = read_poll_timeout(ADF_CSR_RD, status,
				status & ADF_GEN6_PM_INIT_STATE,
				ADF_GEN6_PM_POLL_DELAY_US,
				ADF_GEN6_PM_POLL_TIMEOUT_US, true, addr,
				ADF_GEN6_PM_STATUS);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to power up the device\n");
		return ret;
	}

	dev_dbg(&GET_DEV(accel_dev), "Setting virtual channels for device qat_dev%d\n",
		accel_dev->accel_id);

	ret = adf_gen6_set_vc(accel_dev);
	if (ret)
		dev_err(&GET_DEV(accel_dev), "Failed to set virtual channels\n");

	return ret;
}

static int enable_pm(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = adf_init_admin_pm(accel_dev, ADF_GEN6_PM_DEFAULT_IDLE_FILTER);
	if (ret)
		return ret;

	/* Initialize PM internal data */
	adf_gen6_init_dev_pm_data(accel_dev);

	return 0;
}

static int dev_config(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC);
	if (ret)
		return ret;

	ret = adf_cfg_section_add(accel_dev, "Accelerator0");
	if (ret)
		return ret;

	switch (adf_get_service_enabled(accel_dev)) {
	case SVC_DC:
	case SVC_DCC:
		ret = adf_gen6_comp_dev_config(accel_dev);
		break;
	default:
		ret = adf_gen6_no_dev_config(accel_dev);
		break;
	}
	if (ret)
		return ret;

	__set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);

	return ret;
}

static void adf_gen6_init_rl_data(struct adf_rl_hw_data *rl_data)
{
	rl_data->pciout_tb_offset = ADF_GEN6_RL_TOKEN_PCIEOUT_BUCKET_OFFSET;
	rl_data->pciin_tb_offset = ADF_GEN6_RL_TOKEN_PCIEIN_BUCKET_OFFSET;
	rl_data->r2l_offset = ADF_GEN6_RL_R2L_OFFSET;
	rl_data->l2c_offset = ADF_GEN6_RL_L2C_OFFSET;
	rl_data->c2s_offset = ADF_GEN6_RL_C2S_OFFSET;
	rl_data->pcie_scale_div = ADF_6XXX_RL_PCIE_SCALE_FACTOR_DIV;
	rl_data->pcie_scale_mul = ADF_6XXX_RL_PCIE_SCALE_FACTOR_MUL;
	rl_data->max_tp[SVC_ASYM] = ADF_6XXX_RL_MAX_TP_ASYM;
	rl_data->max_tp[SVC_SYM] = ADF_6XXX_RL_MAX_TP_SYM;
	rl_data->max_tp[SVC_DC] = ADF_6XXX_RL_MAX_TP_DC;
	rl_data->max_tp[SVC_DECOMP] = ADF_6XXX_RL_MAX_TP_DECOMP;
	rl_data->scan_interval = ADF_6XXX_RL_SCANS_PER_SEC;
	rl_data->scale_ref = ADF_6XXX_RL_SLICE_REF;

	init_num_svc_aes(rl_data);
}

void adf_init_hw_data_6xxx(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class = &adf_6xxx_class;
	hw_data->instance_id = adf_6xxx_class.instances++;
	hw_data->num_banks = ADF_GEN6_ETR_MAX_BANKS;
	hw_data->num_banks_per_vf = ADF_GEN6_NUM_BANKS_PER_VF;
	hw_data->num_rings_per_bank = ADF_GEN6_NUM_RINGS_PER_BANK;
	hw_data->num_accel = ADF_GEN6_MAX_ACCELERATORS;
	hw_data->num_engines = ADF_6XXX_MAX_ACCELENGINES;
	hw_data->num_logical_accel = 1;
	hw_data->tx_rx_gap = ADF_GEN6_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_GEN6_TX_RINGS_MASK;
	hw_data->ring_to_svc_map = 0;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = enable_error_correction;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_num_accels = get_num_accels;
	hw_data->get_num_aes = get_num_aes;
	hw_data->get_sram_bar_id = get_sram_bar_id;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_arb_info = get_arb_info;
	hw_data->get_admin_info = get_admin_info;
	hw_data->get_accel_cap = get_accel_cap;
	hw_data->get_sku = get_sku;
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->send_admin_init = adf_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = enable_ints;
	hw_data->reset_device = adf_reset_flr;
	hw_data->admin_ae_mask = ADF_6XXX_ADMIN_AE_MASK;
	hw_data->fw_name = ADF_6XXX_FW;
	hw_data->fw_mmp_name = ADF_6XXX_MMP;
	hw_data->uof_get_name = uof_get_name_6xxx;
	hw_data->uof_get_num_objs = uof_get_num_objs;
	hw_data->uof_get_obj_type = uof_get_obj_type;
	hw_data->uof_get_ae_mask = uof_get_ae_mask;
	hw_data->set_msix_rttable = set_msix_default_rttable;
	hw_data->set_ssm_wdtimer = set_ssm_wdtimer;
	hw_data->get_ring_to_svc_map = get_ring_to_svc_map;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->ring_pair_reset = ring_pair_reset;
	hw_data->dev_config = dev_config;
	hw_data->bank_state_save = adf_bank_state_save;
	hw_data->bank_state_restore = adf_bank_state_restore;
	hw_data->get_hb_clock = get_heartbeat_clock;
	hw_data->num_hb_ctrs = ADF_NUM_HB_CNT_PER_AE;
	hw_data->start_timer = adf_timer_start;
	hw_data->stop_timer = adf_timer_stop;
	hw_data->init_device = adf_init_device;
	hw_data->enable_pm = enable_pm;
	hw_data->services_supported = services_supported;
	hw_data->num_rps = ADF_GEN6_ETR_MAX_BANKS;
	hw_data->clock_frequency = ADF_6XXX_AE_FREQ;
	hw_data->get_svc_slice_cnt = adf_gen6_get_svc_slice_cnt;

	adf_gen6_init_hw_csr_ops(&hw_data->csr_ops);
	adf_gen6_init_pf_pfvf_ops(&hw_data->pfvf_ops);
	adf_gen6_init_dc_ops(&hw_data->dc_ops);
	adf_gen6_init_vf_mig_ops(&hw_data->vfmig_ops);
	adf_gen6_init_ras_ops(&hw_data->ras_ops);
	adf_gen6_init_tl_data(&hw_data->tl_data);
	adf_gen6_init_rl_data(&hw_data->rl_data);
}

void adf_clean_hw_data_6xxx(struct adf_hw_device_data *hw_data)
{
	if (hw_data->dev_class->instances)
		hw_data->dev_class->instances--;
}
