// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * I3C HCI v1.0/v1.1 Command Descriptor Handling
 */

#include <linux/bitfield.h>
#include <linux/i3c/master.h>
#include <linux/i3c/device.h>

#include "hci.h"
#include "cmd.h"
#include "dat.h"
#include "dct.h"


/*
 * Address Assignment Command
 */

#define CMD_0_ATTR_A			FIELD_PREP(CMD_0_ATTR, 0x2)

#define CMD_A0_TOC				   W0_BIT_(31)
#define CMD_A0_ROC				   W0_BIT_(30)
#define CMD_A0_DEV_COUNT(v)		FIELD_PREP(W0_MASK(29, 26), v)
#ifdef CONFIG_ARCH_ASPEED
#define CMD_A0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(22, 16), v)
#else
#define CMD_A0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#endif
#define CMD_A0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_A0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

/*
 * Immediate Data Transfer Command
 */

#define CMD_0_ATTR_I			FIELD_PREP(CMD_0_ATTR, 0x1)

#define CMD_I1_DATA_BYTE_4(v)		FIELD_PREP(W1_MASK(63, 56), v)
#define CMD_I1_DATA_BYTE_3(v)		FIELD_PREP(W1_MASK(55, 48), v)
#define CMD_I1_DATA_BYTE_2(v)		FIELD_PREP(W1_MASK(47, 40), v)
#define CMD_I1_DATA_BYTE_1(v)		FIELD_PREP(W1_MASK(39, 32), v)
#define CMD_I1_DEF_BYTE(v)		FIELD_PREP(W1_MASK(39, 32), v)
#define CMD_I0_TOC				   W0_BIT_(31)
#define CMD_I0_ROC				   W0_BIT_(30)
#define CMD_I0_RNW				   W0_BIT_(29)
#define CMD_I0_MODE(v)			FIELD_PREP(W0_MASK(28, 26), v)
#define CMD_I0_DTT(v)			FIELD_PREP(W0_MASK(25, 23), v)
#ifdef CONFIG_ARCH_ASPEED
#define CMD_I0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(22, 16), v)
#else
#define CMD_I0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#endif
#define CMD_I0_CP				   W0_BIT_(15)
#define CMD_I0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_I0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

/*
 * Regular Data Transfer Command
 */

#define CMD_0_ATTR_R			FIELD_PREP(CMD_0_ATTR, 0x0)

#define CMD_R1_DATA_LENGTH(v)		FIELD_PREP(W1_MASK(63, 48), v)
#define CMD_R1_DEF_BYTE(v)		FIELD_PREP(W1_MASK(39, 32), v)
#define CMD_R0_TOC				   W0_BIT_(31)
#define CMD_R0_ROC				   W0_BIT_(30)
#define CMD_R0_RNW				   W0_BIT_(29)
#define CMD_R0_MODE(v)			FIELD_PREP(W0_MASK(28, 26), v)
#define CMD_R0_DBP				   W0_BIT_(25)
#ifdef CONFIG_ARCH_ASPEED
#define CMD_R0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(22, 16), v)
#else
#define CMD_R0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#endif
#define CMD_R0_CP				   W0_BIT_(15)
#define CMD_R0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_R0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

#ifndef CONFIG_ARCH_ASPEED
/*
 * Combo Transfer (Write + Write/Read) Command
 */

#define CMD_0_ATTR_C			FIELD_PREP(CMD_0_ATTR, 0x3)

#define CMD_C1_DATA_LENGTH(v)		FIELD_PREP(W1_MASK(63, 48), v)
#define CMD_C1_OFFSET(v)		FIELD_PREP(W1_MASK(47, 32), v)
#define CMD_C0_TOC				   W0_BIT_(31)
#define CMD_C0_ROC				   W0_BIT_(30)
#define CMD_C0_RNW				   W0_BIT_(29)
#define CMD_C0_MODE(v)			FIELD_PREP(W0_MASK(28, 26), v)
#define CMD_C0_16_BIT_SUBOFFSET			   W0_BIT_(25)
#define CMD_C0_FIRST_PHASE_MODE			   W0_BIT_(24)
#define CMD_C0_DATA_LENGTH_POSITION(v)	FIELD_PREP(W0_MASK(23, 22), v)
#define CMD_C0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#define CMD_C0_CP				   W0_BIT_(15)
#define CMD_C0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_C0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)
#endif
/*
 * Internal Control Command
 */

#define CMD_0_ATTR_M			FIELD_PREP(CMD_0_ATTR, 0x7)

#define CMD_M1_VENDOR_SPECIFIC			   W1_MASK(63, 32)
#define CMD_M0_MIPI_RESERVED(v)		FIELD_PREP(W0_MASK(31, 12), v)
#define CMD_M0_MIPI_CMD(v)		FIELD_PREP(W0_MASK(11,  8), v)
#define CMD_M0_VENDOR_INFO_PRESENT		   W0_BIT_( 7)
#define CMD_M0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

/*
 * Target Transfer Command
 */

#define CMD_0_ATTR_T			FIELD_PREP(CMD_0_ATTR, 0x0)

#define CMD_T0_DATA_LENGTH(v)		FIELD_PREP(W0_MASK(31, 16), v)
#define CMD_T0_MDB(v)			FIELD_PREP(W0_MASK(15, 8), v)
#define CMD_T0_MDB_EN			W0_BIT_(6)
#define CMD_T0_TID(v)			FIELD_PREP(W0_MASK(5, 3), v)

/* Aspeed in-house register */
#define ASPEED_I3C_CTRL			0x0
#define ASPEED_I3C_CTRL_STOP_QUEUE_PT	BIT(31) //Stop the queue read pointer.
#define ASPEED_I3C_CTRL_INIT		BIT(4)
#define ASPEED_I3C_CTRL_INIT_MODE	GENMASK(1, 0)
#define INIT_MST_MODE 0
#define INIT_SEC_MST_MODE 1
#define INIT_SLV_MODE 2

#define ASPEED_I3C_STS	0x4
#define ASPEED_I3C_STS_SLV_DYNAMIC_ADDRESS_VALID	BIT(23)
#define ASPEED_I3C_STS_SLV_DYNAMIC_ADDRESS		GENMASK(22, 16)
#define ASPEED_I3C_STS_MODE_PURE_SLV			BIT(8)
#define ASPEED_I3C_STS_MODE_SECONDARY_SLV_TO_MST	BIT(7)
#define ASPEED_I3C_STS_MODE_SECONDARY_MST_TO_SLV	BIT(6)
#define ASPEED_I3C_STS_MODE_SECONDARY_SLV		BIT(5)
#define ASPEED_I3C_STS_MODE_SECONDARY_MST		BIT(4)
#define ASPEED_I3C_STS_MODE_PRIMARY_SLV_TO_MST		BIT(3)
#define ASPEED_I3C_STS_MODE_PRIMARY_MST_TO_SLV		BIT(2)
#define ASPEED_I3C_STS_MODE_PRIMARY_SLV			BIT(1)
#define ASPEED_I3C_STS_MODE_PRIMARY_MST			BIT(0)

#define ASPEED_I3C_DAA_INDEX0	0x10
#define ASPEED_I3C_DAA_INDEX1	0x14
#define ASPEED_I3C_DAA_INDEX2	0x18
#define ASPEED_I3C_DAA_INDEX3	0x1C

#define ASPEED_I3C_AUTOCMD_0	0x20
#define ASPEED_I3C_AUTOCMD_1	0x24
#define ASPEED_I3C_AUTOCMD_2	0x28
#define ASPEED_I3C_AUTOCMD_3	0x2C
#define ASPEED_I3C_AUTOCMD_4	0x30
#define ASPEED_I3C_AUTOCMD_5	0x34
#define ASPEED_I3C_AUTOCMD_6	0x38
#define ASPEED_I3C_AUTOCMD_7	0x3C

#define ASPEED_I3C_AUTOCMD_SEL_0_7	0x40
#define ASPEED_I3C_AUTOCMD_SEL_8_15	0x44
#define ASPEED_I3C_AUTOCMD_SEL_16_23	0x48
#define ASPEED_I3C_AUTOCMD_SEL_24_31	0x4C
#define ASPEED_I3C_AUTOCMD_SEL_32_39	0x50
#define ASPEED_I3C_AUTOCMD_SEL_40_47	0x54
#define ASPEED_I3C_AUTOCMD_SEL_48_55	0x58
#define ASPEED_I3C_AUTOCMD_SEL_56_63	0x5C
#define ASPEED_I3C_AUTOCMD_SEL_64_71	0x60
#define ASPEED_I3C_AUTOCMD_SEL_72_79	0x64
#define ASPEED_I3C_AUTOCMD_SEL_80_87	0x68
#define ASPEED_I3C_AUTOCMD_SEL_88_95	0x6C
#define ASPEED_I3C_AUTOCMD_SEL_96_103	0x70
#define ASPEED_I3C_AUTOCMD_SEL_104_111	0x74
#define ASPEED_I3C_AUTOCMD_SEL_112_119	0x78
#define ASPEED_I3C_AUTOCMD_SEL_120_127	0x7C

#define ASPEED_I3C_INTR_STATUS		0xE0
#define ASPEED_I3C_INTR_STATUS_ENABLE	0xE4
#define ASPEED_I3C_INTR_SIGNAL_ENABLE	0xE8
#define ASPEED_I3C_INTR_FORCE		0xEC
#define ASPEED_I3C_INTR_I2C_SDA_STUCK_LOW	BIT(14)
#define ASPEED_I3C_INTR_I3C_SDA_STUCK_HIGH	BIT(13)
#define ASPEED_I3C_INTR_I3C_SDA_STUCK_LOW	BIT(12)
#define ASPEED_I3C_INTR_MST_INTERNAL_DONE	BIT(10)
#define ASPEED_I3C_INTR_MST_DDR_READ_DONE	BIT(9)
#define ASPEED_I3C_INTR_MST_DDR_WRITE_DONE	BIT(8)
#define ASPEED_I3C_INTR_MST_IBI_DONE		BIT(7)
#define ASPEED_I3C_INTR_MST_READ_DONE		BIT(6)
#define ASPEED_I3C_INTR_MST_WRITE_DONE		BIT(5)
#define ASPEED_I3C_INTR_MST_DAA_DONE		BIT(4)
#define ASPEED_I3C_INTR_SLV_SCL_STUCK		BIT(1)
#define ASPEED_I3C_INTR_TGRST			BIT(0)

#define ASPEED_I3C_INTR_SUM_STATUS	0xF0
#define ASPEED_INTR_SUM_INHOUSE		BIT(3)
#define ASPEED_INTR_SUM_RHS		BIT(2)
#define ASPEED_INTR_SUM_PIO		BIT(1)
#define ASPEED_INTR_SUM_CAP		BIT(0)

#define ASPEED_I3C_INTR_RENEW		0xF4

#define ast_inhouse_read(r)		readl(hci->EXTCAPS_regs + (r))
#define ast_inhouse_write(r, v)		writel(v, hci->EXTCAPS_regs + (r))


/* Data Transfer Speed and Mode */
enum hci_cmd_mode {
	MODE_I3C_SDR0		= 0x0,
	MODE_I3C_SDR1		= 0x1,
	MODE_I3C_SDR2		= 0x2,
	MODE_I3C_SDR3		= 0x3,
	MODE_I3C_SDR4		= 0x4,
	MODE_I3C_HDR_TSx	= 0x5,
	MODE_I3C_HDR_DDR	= 0x6,
	MODE_I3C_HDR_BT		= 0x7,
	MODE_I3C_Fm_FmP		= 0x8,
	MODE_I2C_Fm		= 0x0,
	MODE_I2C_FmP		= 0x1,
	MODE_I2C_UD1		= 0x2,
	MODE_I2C_UD2		= 0x3,
	MODE_I2C_UD3		= 0x4,
};

static enum hci_cmd_mode get_i3c_mode(struct i3c_hci *hci)
{
	struct i3c_bus *bus = i3c_master_get_bus(&hci->master);

	if (bus->scl_rate.i3c >= 12500000)
		return MODE_I3C_SDR0;
	if (bus->scl_rate.i3c > 8000000)
		return MODE_I3C_SDR1;
	if (bus->scl_rate.i3c > 6000000)
		return MODE_I3C_SDR2;
	if (bus->scl_rate.i3c > 4000000)
		return MODE_I3C_SDR3;
	if (bus->scl_rate.i3c > 2000000)
		return MODE_I3C_SDR4;
	return MODE_I3C_Fm_FmP;
}

static enum hci_cmd_mode get_i2c_mode(struct i3c_hci *hci)
{
	struct i3c_bus *bus = i3c_master_get_bus(&hci->master);

	if (bus->scl_rate.i2c >= 1000000)
		return MODE_I2C_FmP;
	return MODE_I2C_Fm;
}

static void fill_data_bytes(struct hci_xfer *xfer, u8 *data,
			    unsigned int data_len)
{
	xfer->cmd_desc[1] = 0;
	switch (data_len) {
	case 4:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_4(data[3]);
		fallthrough;
	case 3:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_3(data[2]);
		fallthrough;
	case 2:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_2(data[1]);
		fallthrough;
	case 1:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_1(data[0]);
		fallthrough;
	case 0:
		break;
	}
	/* we consumed all the data with the cmd descriptor */
	xfer->data = NULL;
}

static int hci_cmd_v1_prep_ccc(struct i3c_hci *hci,
			       struct hci_xfer *xfer,
			       u8 ccc_addr, u8 ccc_cmd, bool raw)
{
	unsigned int dat_idx = 0;
	enum hci_cmd_mode mode = get_i3c_mode(hci);
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;
	bool rnw = xfer->rnw;
	int ret;

	/* this should never happen */
	if (WARN_ON(raw))
		return -EINVAL;

	if (ccc_addr != I3C_BROADCAST_ADDR) {
		ret = mipi_i3c_hci_dat_v1.get_index(hci, ccc_addr);
		if (ret < 0)
			return ret;
		dat_idx = ret;
	}

	xfer->cmd_tid = hci_get_tid();

	if (!rnw && data_len <= 4) {
		/* we use an Immediate Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_I |
			CMD_I0_TID(xfer->cmd_tid) |
			CMD_I0_CMD(ccc_cmd) | CMD_I0_CP |
			CMD_I0_DEV_INDEX(dat_idx) |
			CMD_I0_DTT(data_len) |
			CMD_I0_MODE(mode);
		fill_data_bytes(xfer, data, data_len);
	} else {
		/* we use a Regular Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_R |
			CMD_R0_TID(xfer->cmd_tid) |
			CMD_R0_CMD(ccc_cmd) | CMD_R0_CP |
			CMD_R0_DEV_INDEX(dat_idx) |
			CMD_R0_MODE(mode) |
			(rnw ? CMD_R0_RNW : 0);
		xfer->cmd_desc[1] =
			CMD_R1_DATA_LENGTH(data_len);
	}

	return 0;
}

static void hci_cmd_v1_prep_i3c_xfer(struct i3c_hci *hci,
				     struct i3c_dev_desc *dev,
				     struct hci_xfer *xfer)
{
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;

	xfer->cmd_tid = hci_get_tid();

	if (hci->master.target) {
		/*
		 * Target mode needs to prepare two cmd_desc for each xfer:
		 * 1st for IBI (tid = 1)
		 * 2nd for pending read data (tid = 0)
		 * tid will be used to indentify ibi or master read, so this
		 * usage rule can't be violated.
		 */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_T |
			CMD_T0_TID(xfer->cmd_tid % 2) |
			CMD_T0_DATA_LENGTH(data_len);
	} else {
		struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
		unsigned int dat_idx = dev_data->dat_idx;
		enum hci_cmd_mode mode = get_i3c_mode(hci);
		bool rnw = xfer->rnw;

		if (!rnw && data_len <= 4) {
			/* we use an Immediate Data Transfer Command */
			xfer->cmd_desc[0] =
				CMD_0_ATTR_I |
				CMD_I0_TID(xfer->cmd_tid) |
				CMD_I0_DEV_INDEX(dat_idx) |
				CMD_I0_DTT(data_len) |
				CMD_I0_MODE(mode);
			fill_data_bytes(xfer, data, data_len);
		} else {
			/* we use a Regular Data Transfer Command */
			xfer->cmd_desc[0] =
				CMD_0_ATTR_R |
				CMD_R0_TID(xfer->cmd_tid) |
				CMD_R0_DEV_INDEX(dat_idx) |
				CMD_R0_MODE(mode) |
				(rnw ? CMD_R0_RNW : 0);
			xfer->cmd_desc[1] =
				CMD_R1_DATA_LENGTH(data_len);
		}
	}
}

static void hci_cmd_v1_prep_i2c_xfer(struct i3c_hci *hci,
				     struct i2c_dev_desc *dev,
				     struct hci_xfer *xfer)
{
	struct i3c_hci_dev_data *dev_data = i2c_dev_get_master_data(dev);
	unsigned int dat_idx = dev_data->dat_idx;
	enum hci_cmd_mode mode = get_i2c_mode(hci);
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;
	bool rnw = xfer->rnw;

	xfer->cmd_tid = hci_get_tid();

	if (!rnw && data_len <= 4) {
		/* we use an Immediate Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_I |
			CMD_I0_TID(xfer->cmd_tid) |
			CMD_I0_DEV_INDEX(dat_idx) |
			CMD_I0_DTT(data_len) |
			CMD_I0_MODE(mode);
		fill_data_bytes(xfer, data, data_len);
	} else {
		/* we use a Regular Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_R |
			CMD_R0_TID(xfer->cmd_tid) |
			CMD_R0_DEV_INDEX(dat_idx) |
			CMD_R0_MODE(mode) |
			(rnw ? CMD_R0_RNW : 0);
		xfer->cmd_desc[1] =
			CMD_R1_DATA_LENGTH(data_len);
	}
}

static void hci_cmd_v1_prep_internal(struct i3c_hci *hci, struct hci_xfer *xfer,
				     u8 sub_cmd, u32 param)
{
	xfer->cmd_tid = hci_get_tid(hci);
	xfer->cmd_desc[0] = CMD_0_ATTR_M | CMD_M0_TID(xfer->cmd_tid) |
			    CMD_M0_MIPI_CMD(sub_cmd) |
			    CMD_M0_MIPI_RESERVED(param);
	xfer->cmd_desc[1] = 0;
}

static void i3c_aspeed_set_daa_index(struct i3c_hci *hci, u8 addr)
{
	if (addr < 32)
		writel(BIT(addr),
		       hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX0);
	else if ((addr >= 32) && (addr < 64))
		writel(BIT(addr - 32),
		       hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX1);
	else if ((addr >= 64) && (addr < 96))
		writel(BIT(addr - 64),
		       hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX2);
	else
		writel(BIT(addr - 96),
		       hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX3);
}

static int hci_cmd_v1_daa(struct i3c_hci *hci)
{
	struct hci_xfer *xfer;
	int ret, dat_idx = -1;
	u8 next_addr = 0x9;
	u64 pid;
	unsigned int dcr, bcr;
	DECLARE_COMPLETION_ONSTACK(done);

	xfer = hci_alloc_xfer(2);
	if (!xfer)
		return -ENOMEM;

	/*
	 * Simple for now: we allocate a temporary DAT entry, do a single
	 * DAA, register the device which will allocate its own DAT entry
	 * via the core callback, then free the temporary DAT entry.
	 * Loop until there is no more devices to assign an address to.
	 * Yes, there is room for improvements.
	 */
	for (;;) {
#ifndef CONFIG_ARCH_ASPEED
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci);
		if (ret < 0)
			break;
		dat_idx = ret;
#endif
		ret = i3c_master_get_free_addr(&hci->master, next_addr);
		if (ret < 0)
			break;
		next_addr = ret;
#ifdef CONFIG_ARCH_ASPEED
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci, next_addr);
		if (ret < 0)
			break;
		dat_idx = ret;
		i3c_aspeed_set_daa_index(hci, dat_idx);
		DBG("Dat index = %x %x %x %x\n",
		    readl(hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX0),
		    readl(hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX1),
		    readl(hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX2),
		    readl(hci->EXTCAPS_regs + ASPEED_I3C_DAA_INDEX3));
#endif

		DBG("next_addr = 0x%02x, DAA using DAT %d", next_addr, dat_idx);
		mipi_i3c_hci_dat_v1.set_dynamic_addr(hci, dat_idx, next_addr);
		mipi_i3c_hci_dct_index_reset(hci);

		xfer->cmd_tid = hci_get_tid();
		xfer->cmd_desc[0] =
			CMD_0_ATTR_A |
			CMD_A0_TID(xfer->cmd_tid) |
			CMD_A0_CMD(I3C_CCC_ENTDAA) |
			CMD_A0_DEV_INDEX(dat_idx) |
			CMD_A0_DEV_COUNT(1) |
			CMD_A0_ROC | CMD_A0_TOC;
		xfer->cmd_desc[1] = 0;
		xfer->completion = &done;
		hci->io->queue_xfer(hci, xfer, 1);
		if (!wait_for_completion_timeout(&done, HZ) &&
		    hci->io->dequeue_xfer(hci, xfer, 1)) {
			ret = -ETIME;
			break;
		}
		if (RESP_STATUS(xfer[0].response) == RESP_ERR_NACK &&
		    RESP_DATA_LENGTH(xfer->response) == 1) {
			ret = 0;  /* no more devices to be assigned */
			break;
		}
		if (RESP_STATUS(xfer[0].response) != RESP_SUCCESS) {
			if (RESP_STATUS(xfer[0].response) ==
			    RESP_ERR_ADDR_HEADER)
				ret = I3C_ERROR_M2;
			else
				ret = -EIO;
			break;
		}

		i3c_hci_dct_get_val(hci, 0, &pid, &dcr, &bcr);
		DBG("assigned address %#x to device PID=0x%llx DCR=%#x BCR=%#x",
		    next_addr, pid, dcr, bcr);

		mipi_i3c_hci_dat_v1.free_entry(hci, dat_idx);
		dat_idx = -1;

		/*
		 * TODO: Extend the subsystem layer to allow for registering
		 * new device and provide BCR/DCR/PID at the same time.
		 */
		ret = i3c_master_add_i3c_dev_locked(&hci->master, next_addr);
		if (ret)
			break;
	}

	if (dat_idx >= 0)
		mipi_i3c_hci_dat_v1.free_entry(hci, dat_idx);
	hci_free_xfer(xfer, 1);
	return ret;
}

const struct hci_cmd_ops mipi_i3c_hci_cmd_v1 = {
	.prep_ccc		= hci_cmd_v1_prep_ccc,
	.prep_i3c_xfer		= hci_cmd_v1_prep_i3c_xfer,
	.prep_i2c_xfer		= hci_cmd_v1_prep_i2c_xfer,
	.prep_internal		= hci_cmd_v1_prep_internal,
	.perform_daa		= hci_cmd_v1_daa,
};
