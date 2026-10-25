/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU6_BUTTRESS_H
#define IPU6_BUTTRESS_H

#include <linux/completion.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/mutex.h>

struct device;
struct firmware;
struct ipu6_device;
struct ipu6_bus_device;

#define BUTTRESS_PS_FREQ_STEP		25U
#define BUTTRESS_MIN_FORCE_PS_FREQ	(BUTTRESS_PS_FREQ_STEP * 8)
#define BUTTRESS_MAX_FORCE_PS_FREQ	(BUTTRESS_PS_FREQ_STEP * 32)

#define BUTTRESS_IS_FREQ_STEP		25U
#define BUTTRESS_MIN_FORCE_IS_FREQ	(BUTTRESS_IS_FREQ_STEP * 8)
#define BUTTRESS_MAX_FORCE_IS_FREQ	(BUTTRESS_IS_FREQ_STEP * 22)

struct ipu6_buttress_ctrl {
	u32 freq_ctl, pwr_sts_shift, pwr_sts_mask, pwr_sts_on, pwr_sts_off;
	unsigned int ratio;
	unsigned int qos_floor;
	int subsys_id;
};

struct ipu6_buttress_ipc {
	struct completion send_complete;
	struct completion recv_complete;
	u32 recv_data;
};

struct ipu6_buttress_registers {
	/* Registers */
	u32 irq_status;
	u32 irq_clear;
	u32 irq_enable;
	u32 pwr_status;
	u32 security_ctl;
	u32 fw_reset_ctl;
	u32 fabric_cmd;
	u32 tsw_ctl;
	u32 tsc_lo;
	u32 wdt;
	u32 btrs_ctrl;
	u32 csr_in;
	u32 csr_out;
	u32 db0_in;
	u32 db0_out;
	u32 data0_in;
	u32 data0_out;
	u32 sku_id;

	/* Bitmasks */
	u32 irq_is;
	u32 irq_ps;
	u32 irq_all;
	u32 irq_events;
	u32 irq_cse_ipc;
	u32 irq_exec_done;
	u32 irq_sai;
};

struct ipu6_buttress {
	struct mutex power_mutex, auth_mutex, cons_mutex, ipc_mutex;
	struct ipu6_buttress_ipc ipc;
	const struct ipu6_buttress_registers *regs;
	struct list_head constraints;
	u32 wdt_cached_value;
	bool force_suspend;
	u32 ref_clk;
};

struct ipu6_ipc_buttress_bulk_msg {
	u32 cmd;
	u32 expected_resp;
	bool require_resp;
	u8 cmd_size;
};

int ipu6_buttress_ipc_reset(struct ipu6_device *isp);
int ipu6_map_fw_region(struct ipu6_bus_device *sys, const void *data,
		       size_t size, enum dma_data_direction dir,
		       unsigned long attrs);
void ipu6_unmap_fw_region(struct ipu6_bus_device *sys,
			  enum dma_data_direction dir);
int ipu6_buttress_power(struct device *dev,
			const struct ipu6_buttress_ctrl *ctrl, bool on);
bool ipu6_buttress_get_secure_mode(struct ipu6_device *isp);
int ipu6_buttress_authenticate(struct ipu6_device *isp);
int ipu6_buttress_reset_authentication(struct ipu6_device *isp);
bool ipu6_buttress_auth_done(struct ipu6_device *isp);
int ipu6_buttress_start_tsc_sync(struct ipu6_device *isp);
void ipu6_buttress_tsc_read(struct ipu6_device *isp, u64 *val);
u64 ipu6_buttress_tsc_ticks_to_ns(u64 ticks, const struct ipu6_device *isp);

irqreturn_t ipu6_buttress_isr(int irq, void *isp_ptr);
irqreturn_t ipu6_buttress_isr_threaded(int irq, void *isp_ptr);
int ipu6_buttress_init(struct ipu6_device *isp);
void ipu6_buttress_exit(struct ipu6_device *isp);
void ipu6_buttress_csi_port_config(struct ipu6_device *isp,
				   u32 legacy, u32 combo);
void ipu6_buttress_restore(struct ipu6_device *isp);
void ipu7_buttress_wakeup_isys(const struct ipu6_device *isp);
u32 ipu7_buttress_get_isys_freq(struct ipu6_device *isp);
void ipu7_buttress_wakeup_psys(const struct ipu6_device *isp);
u32 ipu7_buttress_get_psys_freq(struct ipu6_device *isp);
#endif /* IPU6_BUTTRESS_H */
