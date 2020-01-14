#ifndef __NVFW_PMU_H__
#define __NVFW_PMU_H__

#define NV_PMU_UNIT_ACR                                                    0x0a

struct nv_pmu_acr_cmd {
	struct nv_falcon_cmd hdr;
#define NV_PMU_ACR_CMD_INIT_WPR_REGION                                     0x00
#define NV_PMU_ACR_CMD_BOOTSTRAP_FALCON                                    0x01
#define NV_PMU_ACR_CMD_BOOTSTRAP_MULTIPLE_FALCONS                          0x03
	u8 cmd_type;
};

struct nv_pmu_acr_msg {
	struct nv_falcon_cmd hdr;
	u8 msg_type;
};

struct nv_pmu_acr_bootstrap_falcon_cmd {
	struct nv_pmu_acr_cmd cmd;
#define NV_PMU_ACR_BOOTSTRAP_FALCON_FLAGS_RESET_YES                  0x00000000
#define NV_PMU_ACR_BOOTSTRAP_FALCON_FLAGS_RESET_NO                   0x00000001
	u32 flags;
	u32 falcon_id;
};

struct nv_pmu_acr_bootstrap_falcon_msg {
	struct nv_pmu_acr_msg msg;
	u32 falcon_id;
};

struct nv_pmu_acr_bootstrap_multiple_falcons_cmd {
	struct nv_pmu_acr_cmd cmd;
#define NV_PMU_ACR_BOOTSTRAP_MULTIPLE_FALCONS_FLAGS_RESET_YES        0x00000000
#define NV_PMU_ACR_BOOTSTRAP_MULTIPLE_FALCONS_FLAGS_RESET_NO         0x00000001
	u32 flags;
	u32 falcon_mask;
	u32 use_va_mask;
	u32 wpr_lo;
	u32 wpr_hi;
};

struct nv_pmu_acr_bootstrap_multiple_falcons_msg {
	struct nv_pmu_acr_msg msg;
	u32 falcon_mask;
};
#endif
