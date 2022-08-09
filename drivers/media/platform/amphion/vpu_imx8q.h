/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_IMX8Q_H
#define _AMPHION_VPU_IMX8Q_H

#define SCB_XREG_SLV_BASE                               0x00000000
#define SCB_SCB_BLK_CTRL                                0x00070000
#define SCB_BLK_CTRL_XMEM_RESET_SET                     0x00000090
#define SCB_BLK_CTRL_CACHE_RESET_SET                    0x000000A0
#define SCB_BLK_CTRL_CACHE_RESET_CLR                    0x000000A4
#define SCB_BLK_CTRL_SCB_CLK_ENABLE_SET                 0x00000100

#define XMEM_CONTROL                                    0x00041000

#define	MC_CACHE_0_BASE					0x00060000
#define	MC_CACHE_1_BASE					0x00068000

#define DEC_MFD_XREG_SLV_BASE                           0x00180000
#define ENC_MFD_XREG_SLV_0_BASE				0x00800000
#define ENC_MFD_XREG_SLV_1_BASE				0x00A00000

#define MFD_HIF                                         0x0001C000
#define MFD_HIF_MSD_REG_INTERRUPT_STATUS                0x00000018
#define MFD_SIF                                         0x0001D000
#define MFD_SIF_CTRL_STATUS                             0x000000F0
#define MFD_SIF_INTR_STATUS                             0x000000F4
#define MFD_MCX                                         0x00020800
#define MFD_MCX_OFF                                     0x00000020
#define MFD_PIX_IF					0x00020000

#define MFD_BLK_CTRL                                    0x00030000
#define MFD_BLK_CTRL_MFD_SYS_RESET_SET                  0x00000000
#define MFD_BLK_CTRL_MFD_SYS_RESET_CLR                  0x00000004
#define MFD_BLK_CTRL_MFD_SYS_CLOCK_ENABLE_SET           0x00000100
#define MFD_BLK_CTRL_MFD_SYS_CLOCK_ENABLE_CLR           0x00000104

#define VID_API_NUM_STREAMS				8
#define VID_API_MAX_BUF_PER_STR				3
#define VID_API_MAX_NUM_MVC_VIEWS			4
#define MEDIAIP_MAX_NUM_MALONES				2
#define MEDIAIP_MAX_NUM_MALONE_IRQ_PINS			2
#define MEDIAIP_MAX_NUM_WINDSORS			1
#define MEDIAIP_MAX_NUM_WINDSOR_IRQ_PINS		2
#define MEDIAIP_MAX_NUM_CMD_IRQ_PINS			2
#define MEDIAIP_MAX_NUM_MSG_IRQ_PINS			1
#define MEDIAIP_MAX_NUM_TIMER_IRQ_PINS			4
#define MEDIAIP_MAX_NUM_TIMER_IRQ_SLOTS			4

#define WINDSOR_PAL_IRQ_PIN_L				0x4
#define WINDSOR_PAL_IRQ_PIN_H				0x5

struct vpu_rpc_system_config {
	u32 cfg_cookie;

	u32 num_malones;
	u32 malone_base_addr[MEDIAIP_MAX_NUM_MALONES];
	u32 hif_offset[MEDIAIP_MAX_NUM_MALONES];
	u32 malone_irq_pin[MEDIAIP_MAX_NUM_MALONES][MEDIAIP_MAX_NUM_MALONE_IRQ_PINS];
	u32 malone_irq_target[MEDIAIP_MAX_NUM_MALONES][MEDIAIP_MAX_NUM_MALONE_IRQ_PINS];

	u32 num_windsors;
	u32 windsor_base_addr[MEDIAIP_MAX_NUM_WINDSORS];
	u32 windsor_irq_pin[MEDIAIP_MAX_NUM_WINDSORS][MEDIAIP_MAX_NUM_WINDSOR_IRQ_PINS];
	u32 windsor_irq_target[MEDIAIP_MAX_NUM_WINDSORS][MEDIAIP_MAX_NUM_WINDSOR_IRQ_PINS];

	u32 cmd_irq_pin[MEDIAIP_MAX_NUM_CMD_IRQ_PINS];
	u32 cmd_irq_target[MEDIAIP_MAX_NUM_CMD_IRQ_PINS];

	u32 msg_irq_pin[MEDIAIP_MAX_NUM_MSG_IRQ_PINS];
	u32 msg_irq_target[MEDIAIP_MAX_NUM_MSG_IRQ_PINS];

	u32 sys_clk_freq;
	u32 num_timers;
	u32 timer_base_addr;
	u32 timer_irq_pin[MEDIAIP_MAX_NUM_TIMER_IRQ_PINS];
	u32 timer_irq_target[MEDIAIP_MAX_NUM_TIMER_IRQ_PINS];
	u32 timer_slots[MEDIAIP_MAX_NUM_TIMER_IRQ_SLOTS];

	u32 gic_base_addr;
	u32 uart_base_addr;

	u32 dpv_base_addr;
	u32 dpv_irq_pin;
	u32 dpv_irq_target;

	u32 pixif_base_addr;

	u32 pal_trace_level;
	u32 pal_trace_destination;

	u32 pal_trace_level1;
	u32 pal_trace_destination1;

	u32 heap_base;
	u32 heap_size;

	u32 cache_base_addr[2];
};

int vpu_imx8q_setup_dec(struct vpu_dev *vpu);
int vpu_imx8q_setup_enc(struct vpu_dev *vpu);
int vpu_imx8q_setup(struct vpu_dev *vpu);
int vpu_imx8q_reset(struct vpu_dev *vpu);
int vpu_imx8q_set_system_cfg_common(struct vpu_rpc_system_config *config, u32 regs, u32 core_id);
int vpu_imx8q_boot_core(struct vpu_core *core);
int vpu_imx8q_get_power_state(struct vpu_core *core);
int vpu_imx8q_on_firmware_loaded(struct vpu_core *core);
int vpu_imx8q_check_memory_region(dma_addr_t base, dma_addr_t addr, u32 size);
bool vpu_imx8q_check_codec(enum vpu_core_type type);
bool vpu_imx8q_check_fmt(enum vpu_core_type type, u32 pixelfmt);

#endif
