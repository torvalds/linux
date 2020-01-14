#ifndef __NVFW_PMU_H__
#define __NVFW_PMU_H__

struct nv_pmu_args {
	u32 reserved;
	u32 freq_hz;
	u32 trace_size;
	u32 trace_dma_base;
	u16 trace_dma_base1;
	u8 trace_dma_offset;
	u32 trace_dma_idx;
	bool secure_mode;
	bool raise_priv_sec;
	struct {
		u32 dma_base;
		u16 dma_base1;
		u8 dma_offset;
		u16 fb_size;
		u8 dma_idx;
	} gc6_ctx;
	u8 pad;
};

#define NV_PMU_UNIT_INIT                                                   0x07
#define NV_PMU_UNIT_ACR                                                    0x0a

struct nv_pmu_init_msg {
	struct nv_falcon_msg hdr;
#define NV_PMU_INIT_MSG_INIT                                               0x00
	u8 msg_type;

	u8 pad;
	u16 os_debug_entry_point;

	struct {
		u16 size;
		u16 offset;
		u8 index;
		u8 pad;
	} queue_info[5];

	u16 sw_managed_area_offset;
	u16 sw_managed_area_size;
};

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

struct nv_pmu_acr_init_wpr_region_cmd {
	struct nv_pmu_acr_cmd cmd;
	u32 region_id;
	u32 wpr_offset;
};

struct nv_pmu_acr_init_wpr_region_msg {
	struct nv_pmu_acr_msg msg;
	u32 error_code;
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
