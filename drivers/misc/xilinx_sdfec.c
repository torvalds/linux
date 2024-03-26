// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SDFEC
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SDFEC16 (Soft Decision FEC 16nm)
 * IP. It exposes a char device which supports file operations
 * like  open(), close() and ioctl().
 */

#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/compat.h>
#include <linux/highmem.h>

#include <uapi/misc/xilinx_sdfec.h>

#define DEV_NAME_LEN 12

static DEFINE_IDA(dev_nrs);

/* Xilinx SDFEC Register Map */
/* CODE_WRI_PROTECT Register */
#define XSDFEC_CODE_WR_PROTECT_ADDR (0x4)

/* ACTIVE Register */
#define XSDFEC_ACTIVE_ADDR (0x8)
#define XSDFEC_IS_ACTIVITY_SET (0x1)

/* AXIS_WIDTH Register */
#define XSDFEC_AXIS_WIDTH_ADDR (0xC)
#define XSDFEC_AXIS_DOUT_WORDS_LSB (5)
#define XSDFEC_AXIS_DOUT_WIDTH_LSB (3)
#define XSDFEC_AXIS_DIN_WORDS_LSB (2)
#define XSDFEC_AXIS_DIN_WIDTH_LSB (0)

/* AXIS_ENABLE Register */
#define XSDFEC_AXIS_ENABLE_ADDR (0x10)
#define XSDFEC_AXIS_OUT_ENABLE_MASK (0x38)
#define XSDFEC_AXIS_IN_ENABLE_MASK (0x7)
#define XSDFEC_AXIS_ENABLE_MASK                                                \
	(XSDFEC_AXIS_OUT_ENABLE_MASK | XSDFEC_AXIS_IN_ENABLE_MASK)

/* FEC_CODE Register */
#define XSDFEC_FEC_CODE_ADDR (0x14)

/* ORDER Register Map */
#define XSDFEC_ORDER_ADDR (0x18)

/* Interrupt Status Register */
#define XSDFEC_ISR_ADDR (0x1C)
/* Interrupt Status Register Bit Mask */
#define XSDFEC_ISR_MASK (0x3F)

/* Write Only - Interrupt Enable Register */
#define XSDFEC_IER_ADDR (0x20)
/* Write Only - Interrupt Disable Register */
#define XSDFEC_IDR_ADDR (0x24)
/* Read Only - Interrupt Mask Register */
#define XSDFEC_IMR_ADDR (0x28)

/* ECC Interrupt Status Register */
#define XSDFEC_ECC_ISR_ADDR (0x2C)
/* Single Bit Errors */
#define XSDFEC_ECC_ISR_SBE_MASK (0x7FF)
/* PL Initialize Single Bit Errors */
#define XSDFEC_PL_INIT_ECC_ISR_SBE_MASK (0x3C00000)
/* Multi Bit Errors */
#define XSDFEC_ECC_ISR_MBE_MASK (0x3FF800)
/* PL Initialize Multi Bit Errors */
#define XSDFEC_PL_INIT_ECC_ISR_MBE_MASK (0x3C000000)
/* Multi Bit Error to Event Shift */
#define XSDFEC_ECC_ISR_MBE_TO_EVENT_SHIFT (11)
/* PL Initialize Multi Bit Error to Event Shift */
#define XSDFEC_PL_INIT_ECC_ISR_MBE_TO_EVENT_SHIFT (4)
/* ECC Interrupt Status Bit Mask */
#define XSDFEC_ECC_ISR_MASK (XSDFEC_ECC_ISR_SBE_MASK | XSDFEC_ECC_ISR_MBE_MASK)
/* ECC Interrupt Status PL Initialize Bit Mask */
#define XSDFEC_PL_INIT_ECC_ISR_MASK                                            \
	(XSDFEC_PL_INIT_ECC_ISR_SBE_MASK | XSDFEC_PL_INIT_ECC_ISR_MBE_MASK)
/* ECC Interrupt Status All Bit Mask */
#define XSDFEC_ALL_ECC_ISR_MASK                                                \
	(XSDFEC_ECC_ISR_MASK | XSDFEC_PL_INIT_ECC_ISR_MASK)
/* ECC Interrupt Status Single Bit Errors Mask */
#define XSDFEC_ALL_ECC_ISR_SBE_MASK                                            \
	(XSDFEC_ECC_ISR_SBE_MASK | XSDFEC_PL_INIT_ECC_ISR_SBE_MASK)
/* ECC Interrupt Status Multi Bit Errors Mask */
#define XSDFEC_ALL_ECC_ISR_MBE_MASK                                            \
	(XSDFEC_ECC_ISR_MBE_MASK | XSDFEC_PL_INIT_ECC_ISR_MBE_MASK)

/* Write Only - ECC Interrupt Enable Register */
#define XSDFEC_ECC_IER_ADDR (0x30)
/* Write Only - ECC Interrupt Disable Register */
#define XSDFEC_ECC_IDR_ADDR (0x34)
/* Read Only - ECC Interrupt Mask Register */
#define XSDFEC_ECC_IMR_ADDR (0x38)

/* BYPASS Register */
#define XSDFEC_BYPASS_ADDR (0x3C)

/* Turbo Code Register */
#define XSDFEC_TURBO_ADDR (0x100)
#define XSDFEC_TURBO_SCALE_MASK (0xFFF)
#define XSDFEC_TURBO_SCALE_BIT_POS (8)
#define XSDFEC_TURBO_SCALE_MAX (15)

/* REG0 Register */
#define XSDFEC_LDPC_CODE_REG0_ADDR_BASE (0x2000)
#define XSDFEC_LDPC_CODE_REG0_ADDR_HIGH (0x27F0)
#define XSDFEC_REG0_N_MIN (4)
#define XSDFEC_REG0_N_MAX (32768)
#define XSDFEC_REG0_N_MUL_P (256)
#define XSDFEC_REG0_N_LSB (0)
#define XSDFEC_REG0_K_MIN (2)
#define XSDFEC_REG0_K_MAX (32766)
#define XSDFEC_REG0_K_MUL_P (256)
#define XSDFEC_REG0_K_LSB (16)

/* REG1 Register */
#define XSDFEC_LDPC_CODE_REG1_ADDR_BASE (0x2004)
#define XSDFEC_LDPC_CODE_REG1_ADDR_HIGH (0x27f4)
#define XSDFEC_REG1_PSIZE_MIN (2)
#define XSDFEC_REG1_PSIZE_MAX (512)
#define XSDFEC_REG1_NO_PACKING_MASK (0x400)
#define XSDFEC_REG1_NO_PACKING_LSB (10)
#define XSDFEC_REG1_NM_MASK (0xFF800)
#define XSDFEC_REG1_NM_LSB (11)
#define XSDFEC_REG1_BYPASS_MASK (0x100000)

/* REG2 Register */
#define XSDFEC_LDPC_CODE_REG2_ADDR_BASE (0x2008)
#define XSDFEC_LDPC_CODE_REG2_ADDR_HIGH (0x27f8)
#define XSDFEC_REG2_NLAYERS_MIN (1)
#define XSDFEC_REG2_NLAYERS_MAX (256)
#define XSDFEC_REG2_NNMQC_MASK (0xFFE00)
#define XSDFEC_REG2_NMQC_LSB (9)
#define XSDFEC_REG2_NORM_TYPE_MASK (0x100000)
#define XSDFEC_REG2_NORM_TYPE_LSB (20)
#define XSDFEC_REG2_SPECIAL_QC_MASK (0x200000)
#define XSDFEC_REG2_SPEICAL_QC_LSB (21)
#define XSDFEC_REG2_NO_FINAL_PARITY_MASK (0x400000)
#define XSDFEC_REG2_NO_FINAL_PARITY_LSB (22)
#define XSDFEC_REG2_MAX_SCHEDULE_MASK (0x1800000)
#define XSDFEC_REG2_MAX_SCHEDULE_LSB (23)

/* REG3 Register */
#define XSDFEC_LDPC_CODE_REG3_ADDR_BASE (0x200C)
#define XSDFEC_LDPC_CODE_REG3_ADDR_HIGH (0x27FC)
#define XSDFEC_REG3_LA_OFF_LSB (8)
#define XSDFEC_REG3_QC_OFF_LSB (16)

#define XSDFEC_LDPC_REG_JUMP (0x10)
#define XSDFEC_REG_WIDTH_JUMP (4)

/* The maximum number of pinned pages */
#define MAX_NUM_PAGES ((XSDFEC_QC_TABLE_DEPTH / PAGE_SIZE) + 1)

/**
 * struct xsdfec_clks - For managing SD-FEC clocks
 * @core_clk: Main processing clock for core
 * @axi_clk: AXI4-Lite memory-mapped clock
 * @din_words_clk: DIN Words AXI4-Stream Slave clock
 * @din_clk: DIN AXI4-Stream Slave clock
 * @dout_clk: DOUT Words AXI4-Stream Slave clock
 * @dout_words_clk: DOUT AXI4-Stream Slave clock
 * @ctrl_clk: Control AXI4-Stream Slave clock
 * @status_clk: Status AXI4-Stream Slave clock
 */
struct xsdfec_clks {
	struct clk *core_clk;
	struct clk *axi_clk;
	struct clk *din_words_clk;
	struct clk *din_clk;
	struct clk *dout_clk;
	struct clk *dout_words_clk;
	struct clk *ctrl_clk;
	struct clk *status_clk;
};

/**
 * struct xsdfec_dev - Driver data for SDFEC
 * @miscdev: Misc device handle
 * @clks: Clocks managed by the SDFEC driver
 * @waitq: Driver wait queue
 * @config: Configuration of the SDFEC device
 * @dev_name: Device name
 * @flags: spinlock flags
 * @regs: device physical base address
 * @dev: pointer to device struct
 * @state: State of the SDFEC device
 * @error_data_lock: Error counter and states spinlock
 * @dev_id: Device ID
 * @isr_err_count: Count of ISR errors
 * @cecc_count: Count of Correctable ECC errors (SBE)
 * @uecc_count: Count of Uncorrectable ECC errors (MBE)
 * @irq: IRQ number
 * @state_updated: indicates State updated by interrupt handler
 * @stats_updated: indicates Stats updated by interrupt handler
 * @intr_enabled: indicates IRQ enabled
 *
 * This structure contains necessary state for SDFEC driver to operate
 */
struct xsdfec_dev {
	struct miscdevice miscdev;
	struct xsdfec_clks clks;
	wait_queue_head_t waitq;
	struct xsdfec_config config;
	char dev_name[DEV_NAME_LEN];
	unsigned long flags;
	void __iomem *regs;
	struct device *dev;
	enum xsdfec_state state;
	/* Spinlock to protect state_updated and stats_updated */
	spinlock_t error_data_lock;
	int dev_id;
	u32 isr_err_count;
	u32 cecc_count;
	u32 uecc_count;
	int irq;
	bool state_updated;
	bool stats_updated;
	bool intr_enabled;
};

static inline void xsdfec_regwrite(struct xsdfec_dev *xsdfec, u32 addr,
				   u32 value)
{
	dev_dbg(xsdfec->dev, "Writing 0x%x to offset 0x%x", value, addr);
	iowrite32(value, xsdfec->regs + addr);
}

static inline u32 xsdfec_regread(struct xsdfec_dev *xsdfec, u32 addr)
{
	u32 rval;

	rval = ioread32(xsdfec->regs + addr);
	dev_dbg(xsdfec->dev, "Read value = 0x%x from offset 0x%x", rval, addr);
	return rval;
}

static void update_bool_config_from_reg(struct xsdfec_dev *xsdfec,
					u32 reg_offset, u32 bit_num,
					char *config_value)
{
	u32 reg_val;
	u32 bit_mask = 1 << bit_num;

	reg_val = xsdfec_regread(xsdfec, reg_offset);
	*config_value = (reg_val & bit_mask) > 0;
}

static void update_config_from_hw(struct xsdfec_dev *xsdfec)
{
	u32 reg_value;
	bool sdfec_started;

	/* Update the Order */
	reg_value = xsdfec_regread(xsdfec, XSDFEC_ORDER_ADDR);
	xsdfec->config.order = reg_value;

	update_bool_config_from_reg(xsdfec, XSDFEC_BYPASS_ADDR,
				    0, /* Bit Number, maybe change to mask */
				    &xsdfec->config.bypass);

	update_bool_config_from_reg(xsdfec, XSDFEC_CODE_WR_PROTECT_ADDR,
				    0, /* Bit Number */
				    &xsdfec->config.code_wr_protect);

	reg_value = xsdfec_regread(xsdfec, XSDFEC_IMR_ADDR);
	xsdfec->config.irq.enable_isr = (reg_value & XSDFEC_ISR_MASK) > 0;

	reg_value = xsdfec_regread(xsdfec, XSDFEC_ECC_IMR_ADDR);
	xsdfec->config.irq.enable_ecc_isr =
		(reg_value & XSDFEC_ECC_ISR_MASK) > 0;

	reg_value = xsdfec_regread(xsdfec, XSDFEC_AXIS_ENABLE_ADDR);
	sdfec_started = (reg_value & XSDFEC_AXIS_IN_ENABLE_MASK) > 0;
	if (sdfec_started)
		xsdfec->state = XSDFEC_STARTED;
	else
		xsdfec->state = XSDFEC_STOPPED;
}

static int xsdfec_get_status(struct xsdfec_dev *xsdfec, void __user *arg)
{
	struct xsdfec_status status;
	int err;

	memset(&status, 0, sizeof(status));
	spin_lock_irqsave(&xsdfec->error_data_lock, xsdfec->flags);
	status.state = xsdfec->state;
	xsdfec->state_updated = false;
	spin_unlock_irqrestore(&xsdfec->error_data_lock, xsdfec->flags);
	status.activity = (xsdfec_regread(xsdfec, XSDFEC_ACTIVE_ADDR) &
			   XSDFEC_IS_ACTIVITY_SET);

	err = copy_to_user(arg, &status, sizeof(status));
	if (err)
		err = -EFAULT;

	return err;
}

static int xsdfec_get_config(struct xsdfec_dev *xsdfec, void __user *arg)
{
	int err;

	err = copy_to_user(arg, &xsdfec->config, sizeof(xsdfec->config));
	if (err)
		err = -EFAULT;

	return err;
}

static int xsdfec_isr_enable(struct xsdfec_dev *xsdfec, bool enable)
{
	u32 mask_read;

	if (enable) {
		/* Enable */
		xsdfec_regwrite(xsdfec, XSDFEC_IER_ADDR, XSDFEC_ISR_MASK);
		mask_read = xsdfec_regread(xsdfec, XSDFEC_IMR_ADDR);
		if (mask_read & XSDFEC_ISR_MASK) {
			dev_dbg(xsdfec->dev,
				"SDFEC enabling irq with IER failed");
			return -EIO;
		}
	} else {
		/* Disable */
		xsdfec_regwrite(xsdfec, XSDFEC_IDR_ADDR, XSDFEC_ISR_MASK);
		mask_read = xsdfec_regread(xsdfec, XSDFEC_IMR_ADDR);
		if ((mask_read & XSDFEC_ISR_MASK) != XSDFEC_ISR_MASK) {
			dev_dbg(xsdfec->dev,
				"SDFEC disabling irq with IDR failed");
			return -EIO;
		}
	}
	return 0;
}

static int xsdfec_ecc_isr_enable(struct xsdfec_dev *xsdfec, bool enable)
{
	u32 mask_read;

	if (enable) {
		/* Enable */
		xsdfec_regwrite(xsdfec, XSDFEC_ECC_IER_ADDR,
				XSDFEC_ALL_ECC_ISR_MASK);
		mask_read = xsdfec_regread(xsdfec, XSDFEC_ECC_IMR_ADDR);
		if (mask_read & XSDFEC_ALL_ECC_ISR_MASK) {
			dev_dbg(xsdfec->dev,
				"SDFEC enabling ECC irq with ECC IER failed");
			return -EIO;
		}
	} else {
		/* Disable */
		xsdfec_regwrite(xsdfec, XSDFEC_ECC_IDR_ADDR,
				XSDFEC_ALL_ECC_ISR_MASK);
		mask_read = xsdfec_regread(xsdfec, XSDFEC_ECC_IMR_ADDR);
		if (!(((mask_read & XSDFEC_ALL_ECC_ISR_MASK) ==
		       XSDFEC_ECC_ISR_MASK) ||
		      ((mask_read & XSDFEC_ALL_ECC_ISR_MASK) ==
		       XSDFEC_PL_INIT_ECC_ISR_MASK))) {
			dev_dbg(xsdfec->dev,
				"SDFEC disable ECC irq with ECC IDR failed");
			return -EIO;
		}
	}
	return 0;
}

static int xsdfec_set_irq(struct xsdfec_dev *xsdfec, void __user *arg)
{
	struct xsdfec_irq irq;
	int err;
	int isr_err;
	int ecc_err;

	err = copy_from_user(&irq, arg, sizeof(irq));
	if (err)
		return -EFAULT;

	/* Setup tlast related IRQ */
	isr_err = xsdfec_isr_enable(xsdfec, irq.enable_isr);
	if (!isr_err)
		xsdfec->config.irq.enable_isr = irq.enable_isr;

	/* Setup ECC related IRQ */
	ecc_err = xsdfec_ecc_isr_enable(xsdfec, irq.enable_ecc_isr);
	if (!ecc_err)
		xsdfec->config.irq.enable_ecc_isr = irq.enable_ecc_isr;

	if (isr_err < 0 || ecc_err < 0)
		err = -EIO;

	return err;
}

static int xsdfec_set_turbo(struct xsdfec_dev *xsdfec, void __user *arg)
{
	struct xsdfec_turbo turbo;
	int err;
	u32 turbo_write;

	err = copy_from_user(&turbo, arg, sizeof(turbo));
	if (err)
		return -EFAULT;

	if (turbo.alg >= XSDFEC_TURBO_ALG_MAX)
		return -EINVAL;

	if (turbo.scale > XSDFEC_TURBO_SCALE_MAX)
		return -EINVAL;

	/* Check to see what device tree says about the FEC codes */
	if (xsdfec->config.code == XSDFEC_LDPC_CODE)
		return -EIO;

	turbo_write = ((turbo.scale & XSDFEC_TURBO_SCALE_MASK)
		       << XSDFEC_TURBO_SCALE_BIT_POS) |
		      turbo.alg;
	xsdfec_regwrite(xsdfec, XSDFEC_TURBO_ADDR, turbo_write);
	return err;
}

static int xsdfec_get_turbo(struct xsdfec_dev *xsdfec, void __user *arg)
{
	u32 reg_value;
	struct xsdfec_turbo turbo_params;
	int err;

	if (xsdfec->config.code == XSDFEC_LDPC_CODE)
		return -EIO;

	memset(&turbo_params, 0, sizeof(turbo_params));
	reg_value = xsdfec_regread(xsdfec, XSDFEC_TURBO_ADDR);

	turbo_params.scale = (reg_value & XSDFEC_TURBO_SCALE_MASK) >>
			     XSDFEC_TURBO_SCALE_BIT_POS;
	turbo_params.alg = reg_value & 0x1;

	err = copy_to_user(arg, &turbo_params, sizeof(turbo_params));
	if (err)
		err = -EFAULT;

	return err;
}

static int xsdfec_reg0_write(struct xsdfec_dev *xsdfec, u32 n, u32 k, u32 psize,
			     u32 offset)
{
	u32 wdata;

	if (n < XSDFEC_REG0_N_MIN || n > XSDFEC_REG0_N_MAX || psize == 0 ||
	    (n > XSDFEC_REG0_N_MUL_P * psize) || n <= k || ((n % psize) != 0)) {
		dev_dbg(xsdfec->dev, "N value is not in range");
		return -EINVAL;
	}
	n <<= XSDFEC_REG0_N_LSB;

	if (k < XSDFEC_REG0_K_MIN || k > XSDFEC_REG0_K_MAX ||
	    (k > XSDFEC_REG0_K_MUL_P * psize) || ((k % psize) != 0)) {
		dev_dbg(xsdfec->dev, "K value is not in range");
		return -EINVAL;
	}
	k = k << XSDFEC_REG0_K_LSB;
	wdata = k | n;

	if (XSDFEC_LDPC_CODE_REG0_ADDR_BASE + (offset * XSDFEC_LDPC_REG_JUMP) >
	    XSDFEC_LDPC_CODE_REG0_ADDR_HIGH) {
		dev_dbg(xsdfec->dev, "Writing outside of LDPC reg0 space 0x%x",
			XSDFEC_LDPC_CODE_REG0_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP));
		return -EINVAL;
	}
	xsdfec_regwrite(xsdfec,
			XSDFEC_LDPC_CODE_REG0_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP),
			wdata);
	return 0;
}

static int xsdfec_reg1_write(struct xsdfec_dev *xsdfec, u32 psize,
			     u32 no_packing, u32 nm, u32 offset)
{
	u32 wdata;

	if (psize < XSDFEC_REG1_PSIZE_MIN || psize > XSDFEC_REG1_PSIZE_MAX) {
		dev_dbg(xsdfec->dev, "Psize is not in range");
		return -EINVAL;
	}

	if (no_packing != 0 && no_packing != 1)
		dev_dbg(xsdfec->dev, "No-packing bit register invalid");
	no_packing = ((no_packing << XSDFEC_REG1_NO_PACKING_LSB) &
		      XSDFEC_REG1_NO_PACKING_MASK);

	if (nm & ~(XSDFEC_REG1_NM_MASK >> XSDFEC_REG1_NM_LSB))
		dev_dbg(xsdfec->dev, "NM is beyond 10 bits");
	nm = (nm << XSDFEC_REG1_NM_LSB) & XSDFEC_REG1_NM_MASK;

	wdata = nm | no_packing | psize;
	if (XSDFEC_LDPC_CODE_REG1_ADDR_BASE + (offset * XSDFEC_LDPC_REG_JUMP) >
	    XSDFEC_LDPC_CODE_REG1_ADDR_HIGH) {
		dev_dbg(xsdfec->dev, "Writing outside of LDPC reg1 space 0x%x",
			XSDFEC_LDPC_CODE_REG1_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP));
		return -EINVAL;
	}
	xsdfec_regwrite(xsdfec,
			XSDFEC_LDPC_CODE_REG1_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP),
			wdata);
	return 0;
}

static int xsdfec_reg2_write(struct xsdfec_dev *xsdfec, u32 nlayers, u32 nmqc,
			     u32 norm_type, u32 special_qc, u32 no_final_parity,
			     u32 max_schedule, u32 offset)
{
	u32 wdata;

	if (nlayers < XSDFEC_REG2_NLAYERS_MIN ||
	    nlayers > XSDFEC_REG2_NLAYERS_MAX) {
		dev_dbg(xsdfec->dev, "Nlayers is not in range");
		return -EINVAL;
	}

	if (nmqc & ~(XSDFEC_REG2_NNMQC_MASK >> XSDFEC_REG2_NMQC_LSB))
		dev_dbg(xsdfec->dev, "NMQC exceeds 11 bits");
	nmqc = (nmqc << XSDFEC_REG2_NMQC_LSB) & XSDFEC_REG2_NNMQC_MASK;

	if (norm_type > 1)
		dev_dbg(xsdfec->dev, "Norm type is invalid");
	norm_type = ((norm_type << XSDFEC_REG2_NORM_TYPE_LSB) &
		     XSDFEC_REG2_NORM_TYPE_MASK);
	if (special_qc > 1)
		dev_dbg(xsdfec->dev, "Special QC in invalid");
	special_qc = ((special_qc << XSDFEC_REG2_SPEICAL_QC_LSB) &
		      XSDFEC_REG2_SPECIAL_QC_MASK);

	if (no_final_parity > 1)
		dev_dbg(xsdfec->dev, "No final parity check invalid");
	no_final_parity =
		((no_final_parity << XSDFEC_REG2_NO_FINAL_PARITY_LSB) &
		 XSDFEC_REG2_NO_FINAL_PARITY_MASK);
	if (max_schedule &
	    ~(XSDFEC_REG2_MAX_SCHEDULE_MASK >> XSDFEC_REG2_MAX_SCHEDULE_LSB))
		dev_dbg(xsdfec->dev, "Max Schedule exceeds 2 bits");
	max_schedule = ((max_schedule << XSDFEC_REG2_MAX_SCHEDULE_LSB) &
			XSDFEC_REG2_MAX_SCHEDULE_MASK);

	wdata = (max_schedule | no_final_parity | special_qc | norm_type |
		 nmqc | nlayers);

	if (XSDFEC_LDPC_CODE_REG2_ADDR_BASE + (offset * XSDFEC_LDPC_REG_JUMP) >
	    XSDFEC_LDPC_CODE_REG2_ADDR_HIGH) {
		dev_dbg(xsdfec->dev, "Writing outside of LDPC reg2 space 0x%x",
			XSDFEC_LDPC_CODE_REG2_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP));
		return -EINVAL;
	}
	xsdfec_regwrite(xsdfec,
			XSDFEC_LDPC_CODE_REG2_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP),
			wdata);
	return 0;
}

static int xsdfec_reg3_write(struct xsdfec_dev *xsdfec, u8 sc_off, u8 la_off,
			     u16 qc_off, u32 offset)
{
	u32 wdata;

	wdata = ((qc_off << XSDFEC_REG3_QC_OFF_LSB) |
		 (la_off << XSDFEC_REG3_LA_OFF_LSB) | sc_off);
	if (XSDFEC_LDPC_CODE_REG3_ADDR_BASE + (offset * XSDFEC_LDPC_REG_JUMP) >
	    XSDFEC_LDPC_CODE_REG3_ADDR_HIGH) {
		dev_dbg(xsdfec->dev, "Writing outside of LDPC reg3 space 0x%x",
			XSDFEC_LDPC_CODE_REG3_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP));
		return -EINVAL;
	}
	xsdfec_regwrite(xsdfec,
			XSDFEC_LDPC_CODE_REG3_ADDR_BASE +
				(offset * XSDFEC_LDPC_REG_JUMP),
			wdata);
	return 0;
}

static int xsdfec_table_write(struct xsdfec_dev *xsdfec, u32 offset,
			      u32 *src_ptr, u32 len, const u32 base_addr,
			      const u32 depth)
{
	u32 reg = 0;
	int res, i, nr_pages;
	u32 n;
	u32 *addr = NULL;
	struct page *pages[MAX_NUM_PAGES];

	/*
	 * Writes that go beyond the length of
	 * Shared Scale(SC) table should fail
	 */
	if (offset > depth / XSDFEC_REG_WIDTH_JUMP ||
	    len > depth / XSDFEC_REG_WIDTH_JUMP ||
	    offset + len > depth / XSDFEC_REG_WIDTH_JUMP) {
		dev_dbg(xsdfec->dev, "Write exceeds SC table length");
		return -EINVAL;
	}

	n = (len * XSDFEC_REG_WIDTH_JUMP) / PAGE_SIZE;
	if ((len * XSDFEC_REG_WIDTH_JUMP) % PAGE_SIZE)
		n += 1;

	if (WARN_ON_ONCE(n > INT_MAX))
		return -EINVAL;

	nr_pages = n;

	res = pin_user_pages_fast((unsigned long)src_ptr, nr_pages, 0, pages);
	if (res < nr_pages) {
		if (res > 0)
			unpin_user_pages(pages, res);

		return -EINVAL;
	}

	for (i = 0; i < nr_pages; i++) {
		addr = kmap_local_page(pages[i]);
		do {
			xsdfec_regwrite(xsdfec,
					base_addr + ((offset + reg) *
						     XSDFEC_REG_WIDTH_JUMP),
					addr[reg]);
			reg++;
		} while ((reg < len) &&
			 ((reg * XSDFEC_REG_WIDTH_JUMP) % PAGE_SIZE));
		kunmap_local(addr);
		unpin_user_page(pages[i]);
	}
	return 0;
}

static int xsdfec_add_ldpc(struct xsdfec_dev *xsdfec, void __user *arg)
{
	struct xsdfec_ldpc_params *ldpc;
	int ret, n;

	ldpc = memdup_user(arg, sizeof(*ldpc));
	if (IS_ERR(ldpc))
		return PTR_ERR(ldpc);

	if (xsdfec->config.code == XSDFEC_TURBO_CODE) {
		ret = -EIO;
		goto err_out;
	}

	/* Verify Device has not started */
	if (xsdfec->state == XSDFEC_STARTED) {
		ret = -EIO;
		goto err_out;
	}

	if (xsdfec->config.code_wr_protect) {
		ret = -EIO;
		goto err_out;
	}

	/* Write Reg 0 */
	ret = xsdfec_reg0_write(xsdfec, ldpc->n, ldpc->k, ldpc->psize,
				ldpc->code_id);
	if (ret)
		goto err_out;

	/* Write Reg 1 */
	ret = xsdfec_reg1_write(xsdfec, ldpc->psize, ldpc->no_packing, ldpc->nm,
				ldpc->code_id);
	if (ret)
		goto err_out;

	/* Write Reg 2 */
	ret = xsdfec_reg2_write(xsdfec, ldpc->nlayers, ldpc->nmqc,
				ldpc->norm_type, ldpc->special_qc,
				ldpc->no_final_parity, ldpc->max_schedule,
				ldpc->code_id);
	if (ret)
		goto err_out;

	/* Write Reg 3 */
	ret = xsdfec_reg3_write(xsdfec, ldpc->sc_off, ldpc->la_off,
				ldpc->qc_off, ldpc->code_id);
	if (ret)
		goto err_out;

	/* Write Shared Codes */
	n = ldpc->nlayers / 4;
	if (ldpc->nlayers % 4)
		n++;

	ret = xsdfec_table_write(xsdfec, ldpc->sc_off, ldpc->sc_table, n,
				 XSDFEC_LDPC_SC_TABLE_ADDR_BASE,
				 XSDFEC_SC_TABLE_DEPTH);
	if (ret < 0)
		goto err_out;

	ret = xsdfec_table_write(xsdfec, 4 * ldpc->la_off, ldpc->la_table,
				 ldpc->nlayers, XSDFEC_LDPC_LA_TABLE_ADDR_BASE,
				 XSDFEC_LA_TABLE_DEPTH);
	if (ret < 0)
		goto err_out;

	ret = xsdfec_table_write(xsdfec, 4 * ldpc->qc_off, ldpc->qc_table,
				 ldpc->nqc, XSDFEC_LDPC_QC_TABLE_ADDR_BASE,
				 XSDFEC_QC_TABLE_DEPTH);
err_out:
	kfree(ldpc);
	return ret;
}

static int xsdfec_set_order(struct xsdfec_dev *xsdfec, void __user *arg)
{
	bool order_invalid;
	enum xsdfec_order order;
	int err;

	err = get_user(order, (enum xsdfec_order __user *)arg);
	if (err)
		return -EFAULT;

	order_invalid = (order != XSDFEC_MAINTAIN_ORDER) &&
			(order != XSDFEC_OUT_OF_ORDER);
	if (order_invalid)
		return -EINVAL;

	/* Verify Device has not started */
	if (xsdfec->state == XSDFEC_STARTED)
		return -EIO;

	xsdfec_regwrite(xsdfec, XSDFEC_ORDER_ADDR, order);

	xsdfec->config.order = order;

	return 0;
}

static int xsdfec_set_bypass(struct xsdfec_dev *xsdfec, bool __user *arg)
{
	bool bypass;
	int err;

	err = get_user(bypass, arg);
	if (err)
		return -EFAULT;

	/* Verify Device has not started */
	if (xsdfec->state == XSDFEC_STARTED)
		return -EIO;

	if (bypass)
		xsdfec_regwrite(xsdfec, XSDFEC_BYPASS_ADDR, 1);
	else
		xsdfec_regwrite(xsdfec, XSDFEC_BYPASS_ADDR, 0);

	xsdfec->config.bypass = bypass;

	return 0;
}

static int xsdfec_is_active(struct xsdfec_dev *xsdfec, bool __user *arg)
{
	u32 reg_value;
	bool is_active;
	int err;

	reg_value = xsdfec_regread(xsdfec, XSDFEC_ACTIVE_ADDR);
	/* using a double ! operator instead of casting */
	is_active = !!(reg_value & XSDFEC_IS_ACTIVITY_SET);
	err = put_user(is_active, arg);
	if (err)
		return -EFAULT;

	return err;
}

static u32
xsdfec_translate_axis_width_cfg_val(enum xsdfec_axis_width axis_width_cfg)
{
	u32 axis_width_field = 0;

	switch (axis_width_cfg) {
	case XSDFEC_1x128b:
		axis_width_field = 0;
		break;
	case XSDFEC_2x128b:
		axis_width_field = 1;
		break;
	case XSDFEC_4x128b:
		axis_width_field = 2;
		break;
	}

	return axis_width_field;
}

static u32 xsdfec_translate_axis_words_cfg_val(enum xsdfec_axis_word_include
	axis_word_inc_cfg)
{
	u32 axis_words_field = 0;

	if (axis_word_inc_cfg == XSDFEC_FIXED_VALUE ||
	    axis_word_inc_cfg == XSDFEC_IN_BLOCK)
		axis_words_field = 0;
	else if (axis_word_inc_cfg == XSDFEC_PER_AXI_TRANSACTION)
		axis_words_field = 1;

	return axis_words_field;
}

static int xsdfec_cfg_axi_streams(struct xsdfec_dev *xsdfec)
{
	u32 reg_value;
	u32 dout_words_field;
	u32 dout_width_field;
	u32 din_words_field;
	u32 din_width_field;
	struct xsdfec_config *config = &xsdfec->config;

	/* translate config info to register values */
	dout_words_field =
		xsdfec_translate_axis_words_cfg_val(config->dout_word_include);
	dout_width_field =
		xsdfec_translate_axis_width_cfg_val(config->dout_width);
	din_words_field =
		xsdfec_translate_axis_words_cfg_val(config->din_word_include);
	din_width_field =
		xsdfec_translate_axis_width_cfg_val(config->din_width);

	reg_value = dout_words_field << XSDFEC_AXIS_DOUT_WORDS_LSB;
	reg_value |= dout_width_field << XSDFEC_AXIS_DOUT_WIDTH_LSB;
	reg_value |= din_words_field << XSDFEC_AXIS_DIN_WORDS_LSB;
	reg_value |= din_width_field << XSDFEC_AXIS_DIN_WIDTH_LSB;

	xsdfec_regwrite(xsdfec, XSDFEC_AXIS_WIDTH_ADDR, reg_value);

	return 0;
}

static int xsdfec_start(struct xsdfec_dev *xsdfec)
{
	u32 regread;

	regread = xsdfec_regread(xsdfec, XSDFEC_FEC_CODE_ADDR);
	regread &= 0x1;
	if (regread != xsdfec->config.code) {
		dev_dbg(xsdfec->dev,
			"%s SDFEC HW code does not match driver code, reg %d, code %d",
			__func__, regread, xsdfec->config.code);
		return -EINVAL;
	}

	/* Set AXIS enable */
	xsdfec_regwrite(xsdfec, XSDFEC_AXIS_ENABLE_ADDR,
			XSDFEC_AXIS_ENABLE_MASK);
	/* Done */
	xsdfec->state = XSDFEC_STARTED;
	return 0;
}

static int xsdfec_stop(struct xsdfec_dev *xsdfec)
{
	u32 regread;

	if (xsdfec->state != XSDFEC_STARTED)
		dev_dbg(xsdfec->dev, "Device not started correctly");
	/* Disable AXIS_ENABLE Input interfaces only */
	regread = xsdfec_regread(xsdfec, XSDFEC_AXIS_ENABLE_ADDR);
	regread &= (~XSDFEC_AXIS_IN_ENABLE_MASK);
	xsdfec_regwrite(xsdfec, XSDFEC_AXIS_ENABLE_ADDR, regread);
	/* Stop */
	xsdfec->state = XSDFEC_STOPPED;
	return 0;
}

static int xsdfec_clear_stats(struct xsdfec_dev *xsdfec)
{
	spin_lock_irqsave(&xsdfec->error_data_lock, xsdfec->flags);
	xsdfec->isr_err_count = 0;
	xsdfec->uecc_count = 0;
	xsdfec->cecc_count = 0;
	spin_unlock_irqrestore(&xsdfec->error_data_lock, xsdfec->flags);

	return 0;
}

static int xsdfec_get_stats(struct xsdfec_dev *xsdfec, void __user *arg)
{
	int err;
	struct xsdfec_stats user_stats;

	spin_lock_irqsave(&xsdfec->error_data_lock, xsdfec->flags);
	user_stats.isr_err_count = xsdfec->isr_err_count;
	user_stats.cecc_count = xsdfec->cecc_count;
	user_stats.uecc_count = xsdfec->uecc_count;
	xsdfec->stats_updated = false;
	spin_unlock_irqrestore(&xsdfec->error_data_lock, xsdfec->flags);

	err = copy_to_user(arg, &user_stats, sizeof(user_stats));
	if (err)
		err = -EFAULT;

	return err;
}

static int xsdfec_set_default_config(struct xsdfec_dev *xsdfec)
{
	/* Ensure registers are aligned with core configuration */
	xsdfec_regwrite(xsdfec, XSDFEC_FEC_CODE_ADDR, xsdfec->config.code);
	xsdfec_cfg_axi_streams(xsdfec);
	update_config_from_hw(xsdfec);

	return 0;
}

static long xsdfec_dev_ioctl(struct file *fptr, unsigned int cmd,
			     unsigned long data)
{
	struct xsdfec_dev *xsdfec;
	void __user *arg = (void __user *)data;
	int rval;

	xsdfec = container_of(fptr->private_data, struct xsdfec_dev, miscdev);

	/* In failed state allow only reset and get status IOCTLs */
	if (xsdfec->state == XSDFEC_NEEDS_RESET &&
	    (cmd != XSDFEC_SET_DEFAULT_CONFIG && cmd != XSDFEC_GET_STATUS &&
	     cmd != XSDFEC_GET_STATS && cmd != XSDFEC_CLEAR_STATS)) {
		return -EPERM;
	}

	switch (cmd) {
	case XSDFEC_START_DEV:
		rval = xsdfec_start(xsdfec);
		break;
	case XSDFEC_STOP_DEV:
		rval = xsdfec_stop(xsdfec);
		break;
	case XSDFEC_CLEAR_STATS:
		rval = xsdfec_clear_stats(xsdfec);
		break;
	case XSDFEC_GET_STATS:
		rval = xsdfec_get_stats(xsdfec, arg);
		break;
	case XSDFEC_GET_STATUS:
		rval = xsdfec_get_status(xsdfec, arg);
		break;
	case XSDFEC_GET_CONFIG:
		rval = xsdfec_get_config(xsdfec, arg);
		break;
	case XSDFEC_SET_DEFAULT_CONFIG:
		rval = xsdfec_set_default_config(xsdfec);
		break;
	case XSDFEC_SET_IRQ:
		rval = xsdfec_set_irq(xsdfec, arg);
		break;
	case XSDFEC_SET_TURBO:
		rval = xsdfec_set_turbo(xsdfec, arg);
		break;
	case XSDFEC_GET_TURBO:
		rval = xsdfec_get_turbo(xsdfec, arg);
		break;
	case XSDFEC_ADD_LDPC_CODE_PARAMS:
		rval = xsdfec_add_ldpc(xsdfec, arg);
		break;
	case XSDFEC_SET_ORDER:
		rval = xsdfec_set_order(xsdfec, arg);
		break;
	case XSDFEC_SET_BYPASS:
		rval = xsdfec_set_bypass(xsdfec, arg);
		break;
	case XSDFEC_IS_ACTIVE:
		rval = xsdfec_is_active(xsdfec, (bool __user *)arg);
		break;
	default:
		rval = -ENOTTY;
		break;
	}
	return rval;
}

static __poll_t xsdfec_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;
	struct xsdfec_dev *xsdfec;

	xsdfec = container_of(file->private_data, struct xsdfec_dev, miscdev);

	poll_wait(file, &xsdfec->waitq, wait);

	/* XSDFEC ISR detected an error */
	spin_lock_irqsave(&xsdfec->error_data_lock, xsdfec->flags);
	if (xsdfec->state_updated)
		mask |= EPOLLIN | EPOLLPRI;

	if (xsdfec->stats_updated)
		mask |= EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&xsdfec->error_data_lock, xsdfec->flags);

	return mask;
}

static const struct file_operations xsdfec_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = xsdfec_dev_ioctl,
	.poll = xsdfec_poll,
	.compat_ioctl = compat_ptr_ioctl,
};

static int xsdfec_parse_of(struct xsdfec_dev *xsdfec)
{
	struct device *dev = xsdfec->dev;
	struct device_node *node = dev->of_node;
	int rval;
	const char *fec_code;
	u32 din_width;
	u32 din_word_include;
	u32 dout_width;
	u32 dout_word_include;

	rval = of_property_read_string(node, "xlnx,sdfec-code", &fec_code);
	if (rval < 0)
		return rval;

	if (!strcasecmp(fec_code, "ldpc"))
		xsdfec->config.code = XSDFEC_LDPC_CODE;
	else if (!strcasecmp(fec_code, "turbo"))
		xsdfec->config.code = XSDFEC_TURBO_CODE;
	else
		return -EINVAL;

	rval = of_property_read_u32(node, "xlnx,sdfec-din-words",
				    &din_word_include);
	if (rval < 0)
		return rval;

	if (din_word_include < XSDFEC_AXIS_WORDS_INCLUDE_MAX)
		xsdfec->config.din_word_include = din_word_include;
	else
		return -EINVAL;

	rval = of_property_read_u32(node, "xlnx,sdfec-din-width", &din_width);
	if (rval < 0)
		return rval;

	switch (din_width) {
	/* Fall through and set for valid values */
	case XSDFEC_1x128b:
	case XSDFEC_2x128b:
	case XSDFEC_4x128b:
		xsdfec->config.din_width = din_width;
		break;
	default:
		return -EINVAL;
	}

	rval = of_property_read_u32(node, "xlnx,sdfec-dout-words",
				    &dout_word_include);
	if (rval < 0)
		return rval;

	if (dout_word_include < XSDFEC_AXIS_WORDS_INCLUDE_MAX)
		xsdfec->config.dout_word_include = dout_word_include;
	else
		return -EINVAL;

	rval = of_property_read_u32(node, "xlnx,sdfec-dout-width", &dout_width);
	if (rval < 0)
		return rval;

	switch (dout_width) {
	/* Fall through and set for valid values */
	case XSDFEC_1x128b:
	case XSDFEC_2x128b:
	case XSDFEC_4x128b:
		xsdfec->config.dout_width = dout_width;
		break;
	default:
		return -EINVAL;
	}

	/* Write LDPC to CODE Register */
	xsdfec_regwrite(xsdfec, XSDFEC_FEC_CODE_ADDR, xsdfec->config.code);

	xsdfec_cfg_axi_streams(xsdfec);

	return 0;
}

static irqreturn_t xsdfec_irq_thread(int irq, void *dev_id)
{
	struct xsdfec_dev *xsdfec = dev_id;
	irqreturn_t ret = IRQ_HANDLED;
	u32 ecc_err;
	u32 isr_err;
	u32 uecc_count;
	u32 cecc_count;
	u32 isr_err_count;
	u32 aecc_count;
	u32 tmp;

	WARN_ON(xsdfec->irq != irq);

	/* Mask Interrupts */
	xsdfec_isr_enable(xsdfec, false);
	xsdfec_ecc_isr_enable(xsdfec, false);
	/* Read ISR */
	ecc_err = xsdfec_regread(xsdfec, XSDFEC_ECC_ISR_ADDR);
	isr_err = xsdfec_regread(xsdfec, XSDFEC_ISR_ADDR);
	/* Clear the interrupts */
	xsdfec_regwrite(xsdfec, XSDFEC_ECC_ISR_ADDR, ecc_err);
	xsdfec_regwrite(xsdfec, XSDFEC_ISR_ADDR, isr_err);

	tmp = ecc_err & XSDFEC_ALL_ECC_ISR_MBE_MASK;
	/* Count uncorrectable 2-bit errors */
	uecc_count = hweight32(tmp);
	/* Count all ECC errors */
	aecc_count = hweight32(ecc_err);
	/* Number of correctable 1-bit ECC error */
	cecc_count = aecc_count - 2 * uecc_count;
	/* Count ISR errors */
	isr_err_count = hweight32(isr_err);
	dev_dbg(xsdfec->dev, "tmp=%x, uecc=%x, aecc=%x, cecc=%x, isr=%x", tmp,
		uecc_count, aecc_count, cecc_count, isr_err_count);
	dev_dbg(xsdfec->dev, "uecc=%x, cecc=%x, isr=%x", xsdfec->uecc_count,
		xsdfec->cecc_count, xsdfec->isr_err_count);

	spin_lock_irqsave(&xsdfec->error_data_lock, xsdfec->flags);
	/* Add new errors to a 2-bits counter */
	if (uecc_count)
		xsdfec->uecc_count += uecc_count;
	/* Add new errors to a 1-bits counter */
	if (cecc_count)
		xsdfec->cecc_count += cecc_count;
	/* Add new errors to a ISR counter */
	if (isr_err_count)
		xsdfec->isr_err_count += isr_err_count;

	/* Update state/stats flag */
	if (uecc_count) {
		if (ecc_err & XSDFEC_ECC_ISR_MBE_MASK)
			xsdfec->state = XSDFEC_NEEDS_RESET;
		else if (ecc_err & XSDFEC_PL_INIT_ECC_ISR_MBE_MASK)
			xsdfec->state = XSDFEC_PL_RECONFIGURE;
		xsdfec->stats_updated = true;
		xsdfec->state_updated = true;
	}

	if (cecc_count)
		xsdfec->stats_updated = true;

	if (isr_err_count) {
		xsdfec->state = XSDFEC_NEEDS_RESET;
		xsdfec->stats_updated = true;
		xsdfec->state_updated = true;
	}

	spin_unlock_irqrestore(&xsdfec->error_data_lock, xsdfec->flags);
	dev_dbg(xsdfec->dev, "state=%x, stats=%x", xsdfec->state_updated,
		xsdfec->stats_updated);

	/* Enable another polling */
	if (xsdfec->state_updated || xsdfec->stats_updated)
		wake_up_interruptible(&xsdfec->waitq);
	else
		ret = IRQ_NONE;

	/* Unmask Interrupts */
	xsdfec_isr_enable(xsdfec, true);
	xsdfec_ecc_isr_enable(xsdfec, true);

	return ret;
}

static int xsdfec_clk_init(struct platform_device *pdev,
			   struct xsdfec_clks *clks)
{
	int err;

	clks->core_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(clks->core_clk)) {
		dev_err(&pdev->dev, "failed to get core_clk");
		return PTR_ERR(clks->core_clk);
	}

	clks->axi_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(clks->axi_clk)) {
		dev_err(&pdev->dev, "failed to get axi_clk");
		return PTR_ERR(clks->axi_clk);
	}

	clks->din_words_clk = devm_clk_get(&pdev->dev, "s_axis_din_words_aclk");
	if (IS_ERR(clks->din_words_clk)) {
		if (PTR_ERR(clks->din_words_clk) != -ENOENT) {
			err = PTR_ERR(clks->din_words_clk);
			return err;
		}
		clks->din_words_clk = NULL;
	}

	clks->din_clk = devm_clk_get(&pdev->dev, "s_axis_din_aclk");
	if (IS_ERR(clks->din_clk)) {
		if (PTR_ERR(clks->din_clk) != -ENOENT) {
			err = PTR_ERR(clks->din_clk);
			return err;
		}
		clks->din_clk = NULL;
	}

	clks->dout_clk = devm_clk_get(&pdev->dev, "m_axis_dout_aclk");
	if (IS_ERR(clks->dout_clk)) {
		if (PTR_ERR(clks->dout_clk) != -ENOENT) {
			err = PTR_ERR(clks->dout_clk);
			return err;
		}
		clks->dout_clk = NULL;
	}

	clks->dout_words_clk =
		devm_clk_get(&pdev->dev, "s_axis_dout_words_aclk");
	if (IS_ERR(clks->dout_words_clk)) {
		if (PTR_ERR(clks->dout_words_clk) != -ENOENT) {
			err = PTR_ERR(clks->dout_words_clk);
			return err;
		}
		clks->dout_words_clk = NULL;
	}

	clks->ctrl_clk = devm_clk_get(&pdev->dev, "s_axis_ctrl_aclk");
	if (IS_ERR(clks->ctrl_clk)) {
		if (PTR_ERR(clks->ctrl_clk) != -ENOENT) {
			err = PTR_ERR(clks->ctrl_clk);
			return err;
		}
		clks->ctrl_clk = NULL;
	}

	clks->status_clk = devm_clk_get(&pdev->dev, "m_axis_status_aclk");
	if (IS_ERR(clks->status_clk)) {
		if (PTR_ERR(clks->status_clk) != -ENOENT) {
			err = PTR_ERR(clks->status_clk);
			return err;
		}
		clks->status_clk = NULL;
	}

	err = clk_prepare_enable(clks->core_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable core_clk (%d)", err);
		return err;
	}

	err = clk_prepare_enable(clks->axi_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable axi_clk (%d)", err);
		goto err_disable_core_clk;
	}

	err = clk_prepare_enable(clks->din_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable din_clk (%d)", err);
		goto err_disable_axi_clk;
	}

	err = clk_prepare_enable(clks->din_words_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable din_words_clk (%d)", err);
		goto err_disable_din_clk;
	}

	err = clk_prepare_enable(clks->dout_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dout_clk (%d)", err);
		goto err_disable_din_words_clk;
	}

	err = clk_prepare_enable(clks->dout_words_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable dout_words_clk (%d)",
			err);
		goto err_disable_dout_clk;
	}

	err = clk_prepare_enable(clks->ctrl_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable ctrl_clk (%d)", err);
		goto err_disable_dout_words_clk;
	}

	err = clk_prepare_enable(clks->status_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable status_clk (%d)\n", err);
		goto err_disable_ctrl_clk;
	}

	return err;

err_disable_ctrl_clk:
	clk_disable_unprepare(clks->ctrl_clk);
err_disable_dout_words_clk:
	clk_disable_unprepare(clks->dout_words_clk);
err_disable_dout_clk:
	clk_disable_unprepare(clks->dout_clk);
err_disable_din_words_clk:
	clk_disable_unprepare(clks->din_words_clk);
err_disable_din_clk:
	clk_disable_unprepare(clks->din_clk);
err_disable_axi_clk:
	clk_disable_unprepare(clks->axi_clk);
err_disable_core_clk:
	clk_disable_unprepare(clks->core_clk);

	return err;
}

static void xsdfec_disable_all_clks(struct xsdfec_clks *clks)
{
	clk_disable_unprepare(clks->status_clk);
	clk_disable_unprepare(clks->ctrl_clk);
	clk_disable_unprepare(clks->dout_words_clk);
	clk_disable_unprepare(clks->dout_clk);
	clk_disable_unprepare(clks->din_words_clk);
	clk_disable_unprepare(clks->din_clk);
	clk_disable_unprepare(clks->core_clk);
	clk_disable_unprepare(clks->axi_clk);
}

static int xsdfec_probe(struct platform_device *pdev)
{
	struct xsdfec_dev *xsdfec;
	struct device *dev;
	int err;
	bool irq_enabled = true;

	xsdfec = devm_kzalloc(&pdev->dev, sizeof(*xsdfec), GFP_KERNEL);
	if (!xsdfec)
		return -ENOMEM;

	xsdfec->dev = &pdev->dev;
	spin_lock_init(&xsdfec->error_data_lock);

	err = xsdfec_clk_init(pdev, &xsdfec->clks);
	if (err)
		return err;

	dev = xsdfec->dev;
	xsdfec->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xsdfec->regs)) {
		err = PTR_ERR(xsdfec->regs);
		goto err_xsdfec_dev;
	}

	xsdfec->irq = platform_get_irq(pdev, 0);
	if (xsdfec->irq < 0) {
		dev_dbg(dev, "platform_get_irq failed");
		irq_enabled = false;
	}

	err = xsdfec_parse_of(xsdfec);
	if (err < 0)
		goto err_xsdfec_dev;

	update_config_from_hw(xsdfec);

	/* Save driver private data */
	platform_set_drvdata(pdev, xsdfec);

	if (irq_enabled) {
		init_waitqueue_head(&xsdfec->waitq);
		/* Register IRQ thread */
		err = devm_request_threaded_irq(dev, xsdfec->irq, NULL,
						xsdfec_irq_thread, IRQF_ONESHOT,
						"xilinx-sdfec16", xsdfec);
		if (err < 0) {
			dev_err(dev, "unable to request IRQ%d", xsdfec->irq);
			goto err_xsdfec_dev;
		}
	}

	err = ida_alloc(&dev_nrs, GFP_KERNEL);
	if (err < 0)
		goto err_xsdfec_dev;
	xsdfec->dev_id = err;

	snprintf(xsdfec->dev_name, DEV_NAME_LEN, "xsdfec%d", xsdfec->dev_id);
	xsdfec->miscdev.minor = MISC_DYNAMIC_MINOR;
	xsdfec->miscdev.name = xsdfec->dev_name;
	xsdfec->miscdev.fops = &xsdfec_fops;
	xsdfec->miscdev.parent = dev;
	err = misc_register(&xsdfec->miscdev);
	if (err) {
		dev_err(dev, "error:%d. Unable to register device", err);
		goto err_xsdfec_ida;
	}
	return 0;

err_xsdfec_ida:
	ida_free(&dev_nrs, xsdfec->dev_id);
err_xsdfec_dev:
	xsdfec_disable_all_clks(&xsdfec->clks);
	return err;
}

static void xsdfec_remove(struct platform_device *pdev)
{
	struct xsdfec_dev *xsdfec;

	xsdfec = platform_get_drvdata(pdev);
	misc_deregister(&xsdfec->miscdev);
	ida_free(&dev_nrs, xsdfec->dev_id);
	xsdfec_disable_all_clks(&xsdfec->clks);
}

static const struct of_device_id xsdfec_of_match[] = {
	{
		.compatible = "xlnx,sd-fec-1.1",
	},
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, xsdfec_of_match);

static struct platform_driver xsdfec_driver = {
	.driver = {
		.name = "xilinx-sdfec",
		.of_match_table = xsdfec_of_match,
	},
	.probe = xsdfec_probe,
	.remove_new =  xsdfec_remove,
};

module_platform_driver(xsdfec_driver);

MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("Xilinx SD-FEC16 Driver");
MODULE_LICENSE("GPL");
