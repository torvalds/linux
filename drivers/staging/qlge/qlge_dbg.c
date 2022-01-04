// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>

#include "qlge.h"

/* Read a NIC register from the alternate function. */
static u32 qlge_read_other_func_reg(struct qlge_adapter *qdev,
				    u32 reg)
{
	u32 register_to_read;
	u32 reg_val;
	unsigned int status = 0;

	register_to_read = MPI_NIC_REG_BLOCK
				| MPI_NIC_READ
				| (qdev->alt_func << MPI_NIC_FUNCTION_SHIFT)
				| reg;
	status = qlge_read_mpi_reg(qdev, register_to_read, &reg_val);
	if (status != 0)
		return 0xffffffff;

	return reg_val;
}

/* Write a NIC register from the alternate function. */
static int qlge_write_other_func_reg(struct qlge_adapter *qdev,
				     u32 reg, u32 reg_val)
{
	u32 register_to_read;

	register_to_read = MPI_NIC_REG_BLOCK
				| MPI_NIC_READ
				| (qdev->alt_func << MPI_NIC_FUNCTION_SHIFT)
				| reg;

	return qlge_write_mpi_reg(qdev, register_to_read, reg_val);
}

static int qlge_wait_other_func_reg_rdy(struct qlge_adapter *qdev, u32 reg,
					u32 bit, u32 err_bit)
{
	u32 temp;
	int count;

	for (count = 10; count; count--) {
		temp = qlge_read_other_func_reg(qdev, reg);

		/* check for errors */
		if (temp & err_bit)
			return -1;
		else if (temp & bit)
			return 0;
		mdelay(10);
	}
	return -1;
}

static int qlge_read_other_func_serdes_reg(struct qlge_adapter *qdev, u32 reg,
					   u32 *data)
{
	int status;

	/* wait for reg to come ready */
	status = qlge_wait_other_func_reg_rdy(qdev, XG_SERDES_ADDR / 4,
					      XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* set up for reg read */
	qlge_write_other_func_reg(qdev, XG_SERDES_ADDR / 4, reg | PROC_ADDR_R);

	/* wait for reg to come ready */
	status = qlge_wait_other_func_reg_rdy(qdev, XG_SERDES_ADDR / 4,
					      XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* get the data */
	*data = qlge_read_other_func_reg(qdev, (XG_SERDES_DATA / 4));
exit:
	return status;
}

/* Read out the SERDES registers */
static int qlge_read_serdes_reg(struct qlge_adapter *qdev, u32 reg, u32 *data)
{
	int status;

	/* wait for reg to come ready */
	status = qlge_wait_reg_rdy(qdev, XG_SERDES_ADDR, XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* set up for reg read */
	qlge_write32(qdev, XG_SERDES_ADDR, reg | PROC_ADDR_R);

	/* wait for reg to come ready */
	status = qlge_wait_reg_rdy(qdev, XG_SERDES_ADDR, XG_SERDES_ADDR_RDY, 0);
	if (status)
		goto exit;

	/* get the data */
	*data = qlge_read32(qdev, XG_SERDES_DATA);
exit:
	return status;
}

static void qlge_get_both_serdes(struct qlge_adapter *qdev, u32 addr,
				 u32 *direct_ptr, u32 *indirect_ptr,
				 bool direct_valid, bool indirect_valid)
{
	unsigned int status;

	status = 1;
	if (direct_valid)
		status = qlge_read_serdes_reg(qdev, addr, direct_ptr);
	/* Dead fill any failures or invalids. */
	if (status)
		*direct_ptr = 0xDEADBEEF;

	status = 1;
	if (indirect_valid)
		status = qlge_read_other_func_serdes_reg(qdev, addr,
							 indirect_ptr);
	/* Dead fill any failures or invalids. */
	if (status)
		*indirect_ptr = 0xDEADBEEF;
}

static int qlge_get_serdes_regs(struct qlge_adapter *qdev,
				struct qlge_mpi_coredump *mpi_coredump)
{
	int status;
	bool xfi_direct_valid = false, xfi_indirect_valid = false;
	bool xaui_direct_valid = true, xaui_indirect_valid = true;
	unsigned int i;
	u32 *direct_ptr, temp;
	u32 *indirect_ptr;

	/* The XAUI needs to be read out per port */
	status = qlge_read_other_func_serdes_reg(qdev,
						 XG_SERDES_XAUI_HSS_PCS_START,
						 &temp);
	if (status)
		temp = XG_SERDES_ADDR_XAUI_PWR_DOWN;

	if ((temp & XG_SERDES_ADDR_XAUI_PWR_DOWN) ==
				XG_SERDES_ADDR_XAUI_PWR_DOWN)
		xaui_indirect_valid = false;

	status = qlge_read_serdes_reg(qdev, XG_SERDES_XAUI_HSS_PCS_START, &temp);

	if (status)
		temp = XG_SERDES_ADDR_XAUI_PWR_DOWN;

	if ((temp & XG_SERDES_ADDR_XAUI_PWR_DOWN) ==
				XG_SERDES_ADDR_XAUI_PWR_DOWN)
		xaui_direct_valid = false;

	/*
	 * XFI register is shared so only need to read one
	 * functions and then check the bits.
	 */
	status = qlge_read_serdes_reg(qdev, XG_SERDES_ADDR_STS, &temp);
	if (status)
		temp = 0;

	if ((temp & XG_SERDES_ADDR_XFI1_PWR_UP) ==
					XG_SERDES_ADDR_XFI1_PWR_UP) {
		/* now see if i'm NIC 1 or NIC 2 */
		if (qdev->func & 1)
			/* I'm NIC 2, so the indirect (NIC1) xfi is up. */
			xfi_indirect_valid = true;
		else
			xfi_direct_valid = true;
	}
	if ((temp & XG_SERDES_ADDR_XFI2_PWR_UP) ==
					XG_SERDES_ADDR_XFI2_PWR_UP) {
		/* now see if i'm NIC 1 or NIC 2 */
		if (qdev->func & 1)
			/* I'm NIC 2, so the indirect (NIC1) xfi is up. */
			xfi_direct_valid = true;
		else
			xfi_indirect_valid = true;
	}

	/* Get XAUI_AN register block. */
	if (qdev->func & 1) {
		/* Function 2 is direct	*/
		direct_ptr = mpi_coredump->serdes2_xaui_an;
		indirect_ptr = mpi_coredump->serdes_xaui_an;
	} else {
		/* Function 1 is direct	*/
		direct_ptr = mpi_coredump->serdes_xaui_an;
		indirect_ptr = mpi_coredump->serdes2_xaui_an;
	}

	for (i = 0; i <= 0x000000034; i += 4, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xaui_direct_valid, xaui_indirect_valid);

	/* Get XAUI_HSS_PCS register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xaui_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes_xaui_hss_pcs;
	} else {
		direct_ptr =
			mpi_coredump->serdes_xaui_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes2_xaui_hss_pcs;
	}

	for (i = 0x800; i <= 0x880; i += 4, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xaui_direct_valid, xaui_indirect_valid);

	/* Get XAUI_XFI_AN register block. */
	if (qdev->func & 1) {
		direct_ptr = mpi_coredump->serdes2_xfi_an;
		indirect_ptr = mpi_coredump->serdes_xfi_an;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_an;
		indirect_ptr = mpi_coredump->serdes2_xfi_an;
	}

	for (i = 0x1000; i <= 0x1034; i += 4, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_TRAIN register block. */
	if (qdev->func & 1) {
		direct_ptr = mpi_coredump->serdes2_xfi_train;
		indirect_ptr =
			mpi_coredump->serdes_xfi_train;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_train;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_train;
	}

	for (i = 0x1050; i <= 0x107c; i += 4, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_HSS_PCS register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_pcs;
	} else {
		direct_ptr =
			mpi_coredump->serdes_xfi_hss_pcs;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_pcs;
	}

	for (i = 0x1800; i <= 0x1838; i += 4, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_HSS_TX register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_tx;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_tx;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_hss_tx;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_tx;
	}
	for (i = 0x1c00; i <= 0x1c1f; i++, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_HSS_RX register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_rx;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_rx;
	} else {
		direct_ptr = mpi_coredump->serdes_xfi_hss_rx;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_rx;
	}

	for (i = 0x1c40; i <= 0x1c5f; i++, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xfi_direct_valid, xfi_indirect_valid);

	/* Get XAUI_XFI_HSS_PLL register block. */
	if (qdev->func & 1) {
		direct_ptr =
			mpi_coredump->serdes2_xfi_hss_pll;
		indirect_ptr =
			mpi_coredump->serdes_xfi_hss_pll;
	} else {
		direct_ptr =
			mpi_coredump->serdes_xfi_hss_pll;
		indirect_ptr =
			mpi_coredump->serdes2_xfi_hss_pll;
	}
	for (i = 0x1e00; i <= 0x1e1f; i++, direct_ptr++, indirect_ptr++)
		qlge_get_both_serdes(qdev, i, direct_ptr, indirect_ptr,
				     xfi_direct_valid, xfi_indirect_valid);
	return 0;
}

static int qlge_read_other_func_xgmac_reg(struct qlge_adapter *qdev, u32 reg,
					  u32 *data)
{
	int status = 0;

	/* wait for reg to come ready */
	status = qlge_wait_other_func_reg_rdy(qdev, XGMAC_ADDR / 4,
					      XGMAC_ADDR_RDY, XGMAC_ADDR_XME);
	if (status)
		goto exit;

	/* set up for reg read */
	qlge_write_other_func_reg(qdev, XGMAC_ADDR / 4, reg | XGMAC_ADDR_R);

	/* wait for reg to come ready */
	status = qlge_wait_other_func_reg_rdy(qdev, XGMAC_ADDR / 4,
					      XGMAC_ADDR_RDY, XGMAC_ADDR_XME);
	if (status)
		goto exit;

	/* get the data */
	*data = qlge_read_other_func_reg(qdev, XGMAC_DATA / 4);
exit:
	return status;
}

/* Read the 400 xgmac control/statistics registers
 * skipping unused locations.
 */
static int qlge_get_xgmac_regs(struct qlge_adapter *qdev, u32 *buf,
			       unsigned int other_function)
{
	int status = 0;
	int i;

	for (i = PAUSE_SRC_LO; i < XGMAC_REGISTER_END; i += 4, buf++) {
		/* We're reading 400 xgmac registers, but we filter out
		 * several locations that are non-responsive to reads.
		 */
		if ((i == 0x00000114) ||
		    (i == 0x00000118) ||
			(i == 0x0000013c) ||
			(i == 0x00000140) ||
			(i > 0x00000150 && i < 0x000001fc) ||
			(i > 0x00000278 && i < 0x000002a0) ||
			(i > 0x000002c0 && i < 0x000002cf) ||
			(i > 0x000002dc && i < 0x000002f0) ||
			(i > 0x000003c8 && i < 0x00000400) ||
			(i > 0x00000400 && i < 0x00000410) ||
			(i > 0x00000410 && i < 0x00000420) ||
			(i > 0x00000420 && i < 0x00000430) ||
			(i > 0x00000430 && i < 0x00000440) ||
			(i > 0x00000440 && i < 0x00000450) ||
			(i > 0x00000450 && i < 0x00000500) ||
			(i > 0x0000054c && i < 0x00000568) ||
			(i > 0x000005c8 && i < 0x00000600)) {
			if (other_function)
				status =
				qlge_read_other_func_xgmac_reg(qdev, i, buf);
			else
				status = qlge_read_xgmac_reg(qdev, i, buf);

			if (status)
				*buf = 0xdeadbeef;
			break;
		}
	}
	return status;
}

static int qlge_get_ets_regs(struct qlge_adapter *qdev, u32 *buf)
{
	int i;

	for (i = 0; i < 8; i++, buf++) {
		qlge_write32(qdev, NIC_ETS, i << 29 | 0x08000000);
		*buf = qlge_read32(qdev, NIC_ETS);
	}

	for (i = 0; i < 2; i++, buf++) {
		qlge_write32(qdev, CNA_ETS, i << 29 | 0x08000000);
		*buf = qlge_read32(qdev, CNA_ETS);
	}

	return 0;
}

static void qlge_get_intr_states(struct qlge_adapter *qdev, u32 *buf)
{
	int i;

	for (i = 0; i < qdev->rx_ring_count; i++, buf++) {
		qlge_write32(qdev, INTR_EN,
			     qdev->intr_context[i].intr_read_mask);
		*buf = qlge_read32(qdev, INTR_EN);
	}
}

static int qlge_get_cam_entries(struct qlge_adapter *qdev, u32 *buf)
{
	int i, status;
	u32 value[3];

	status = qlge_sem_spinlock(qdev, SEM_MAC_ADDR_MASK);
	if (status)
		return status;

	for (i = 0; i < 16; i++) {
		status = qlge_get_mac_addr_reg(qdev,
					       MAC_ADDR_TYPE_CAM_MAC, i, value);
		if (status) {
			netif_err(qdev, drv, qdev->ndev,
				  "Failed read of mac index register\n");
			goto err;
		}
		*buf++ = value[0];	/* lower MAC address */
		*buf++ = value[1];	/* upper MAC address */
		*buf++ = value[2];	/* output */
	}
	for (i = 0; i < 32; i++) {
		status = qlge_get_mac_addr_reg(qdev, MAC_ADDR_TYPE_MULTI_MAC,
					       i, value);
		if (status) {
			netif_err(qdev, drv, qdev->ndev,
				  "Failed read of mac index register\n");
			goto err;
		}
		*buf++ = value[0];	/* lower Mcast address */
		*buf++ = value[1];	/* upper Mcast address */
	}
err:
	qlge_sem_unlock(qdev, SEM_MAC_ADDR_MASK);
	return status;
}

static int qlge_get_routing_entries(struct qlge_adapter *qdev, u32 *buf)
{
	int status;
	u32 value, i;

	status = qlge_sem_spinlock(qdev, SEM_RT_IDX_MASK);
	if (status)
		return status;

	for (i = 0; i < 16; i++) {
		status = qlge_get_routing_reg(qdev, i, &value);
		if (status) {
			netif_err(qdev, drv, qdev->ndev,
				  "Failed read of routing index register\n");
			goto err;
		} else {
			*buf++ = value;
		}
	}
err:
	qlge_sem_unlock(qdev, SEM_RT_IDX_MASK);
	return status;
}

/* Read the MPI Processor shadow registers */
static int qlge_get_mpi_shadow_regs(struct qlge_adapter *qdev, u32 *buf)
{
	u32 i;
	int status;

	for (i = 0; i < MPI_CORE_SH_REGS_CNT; i++, buf++) {
		status = qlge_write_mpi_reg(qdev,
					    RISC_124,
					    (SHADOW_OFFSET | i << SHADOW_REG_SHIFT));
		if (status)
			goto end;
		status = qlge_read_mpi_reg(qdev, RISC_127, buf);
		if (status)
			goto end;
	}
end:
	return status;
}

/* Read the MPI Processor core registers */
static int qlge_get_mpi_regs(struct qlge_adapter *qdev, u32 *buf,
			     u32 offset, u32 count)
{
	int i, status = 0;

	for (i = 0; i < count; i++, buf++) {
		status = qlge_read_mpi_reg(qdev, offset + i, buf);
		if (status)
			return status;
	}
	return status;
}

/* Read the ASIC probe dump */
static unsigned int *qlge_get_probe(struct qlge_adapter *qdev, u32 clock,
				    u32 valid, u32 *buf)
{
	u32 module, mux_sel, probe, lo_val, hi_val;

	for (module = 0; module < PRB_MX_ADDR_MAX_MODS; module++) {
		if (!((valid >> module) & 1))
			continue;
		for (mux_sel = 0; mux_sel < PRB_MX_ADDR_MAX_MUX; mux_sel++) {
			probe = clock
				| PRB_MX_ADDR_ARE
				| mux_sel
				| (module << PRB_MX_ADDR_MOD_SEL_SHIFT);
			qlge_write32(qdev, PRB_MX_ADDR, probe);
			lo_val = qlge_read32(qdev, PRB_MX_DATA);
			if (mux_sel == 0) {
				*buf = probe;
				buf++;
			}
			probe |= PRB_MX_ADDR_UP;
			qlge_write32(qdev, PRB_MX_ADDR, probe);
			hi_val = qlge_read32(qdev, PRB_MX_DATA);
			*buf = lo_val;
			buf++;
			*buf = hi_val;
			buf++;
		}
	}
	return buf;
}

static int qlge_get_probe_dump(struct qlge_adapter *qdev, unsigned int *buf)
{
	/* First we have to enable the probe mux */
	qlge_write_mpi_reg(qdev, MPI_TEST_FUNC_PRB_CTL, MPI_TEST_FUNC_PRB_EN);
	buf = qlge_get_probe(qdev, PRB_MX_ADDR_SYS_CLOCK,
			     PRB_MX_ADDR_VALID_SYS_MOD, buf);
	buf = qlge_get_probe(qdev, PRB_MX_ADDR_PCI_CLOCK,
			     PRB_MX_ADDR_VALID_PCI_MOD, buf);
	buf = qlge_get_probe(qdev, PRB_MX_ADDR_XGM_CLOCK,
			     PRB_MX_ADDR_VALID_XGM_MOD, buf);
	buf = qlge_get_probe(qdev, PRB_MX_ADDR_FC_CLOCK,
			     PRB_MX_ADDR_VALID_FC_MOD, buf);
	return 0;
}

/* Read out the routing index registers */
static int qlge_get_routing_index_registers(struct qlge_adapter *qdev, u32 *buf)
{
	int status;
	u32 type, index, index_max;
	u32 result_index;
	u32 result_data;
	u32 val;

	status = qlge_sem_spinlock(qdev, SEM_RT_IDX_MASK);
	if (status)
		return status;

	for (type = 0; type < 4; type++) {
		if (type < 2)
			index_max = 8;
		else
			index_max = 16;
		for (index = 0; index < index_max; index++) {
			val = RT_IDX_RS
				| (type << RT_IDX_TYPE_SHIFT)
				| (index << RT_IDX_IDX_SHIFT);
			qlge_write32(qdev, RT_IDX, val);
			result_index = 0;
			while ((result_index & RT_IDX_MR) == 0)
				result_index = qlge_read32(qdev, RT_IDX);
			result_data = qlge_read32(qdev, RT_DATA);
			*buf = type;
			buf++;
			*buf = index;
			buf++;
			*buf = result_index;
			buf++;
			*buf = result_data;
			buf++;
		}
	}
	qlge_sem_unlock(qdev, SEM_RT_IDX_MASK);
	return status;
}

/* Read out the MAC protocol registers */
static void qlge_get_mac_protocol_registers(struct qlge_adapter *qdev, u32 *buf)
{
	u32 result_index, result_data;
	u32 type;
	u32 index;
	u32 offset;
	u32 val;
	u32 initial_val = MAC_ADDR_RS;
	u32 max_index;
	u32 max_offset;

	for (type = 0; type < MAC_ADDR_TYPE_COUNT; type++) {
		switch (type) {
		case 0: /* CAM */
			initial_val |= MAC_ADDR_ADR;
			max_index = MAC_ADDR_MAX_CAM_ENTRIES;
			max_offset = MAC_ADDR_MAX_CAM_WCOUNT;
			break;
		case 1: /* Multicast MAC Address */
			max_index = MAC_ADDR_MAX_CAM_WCOUNT;
			max_offset = MAC_ADDR_MAX_CAM_WCOUNT;
			break;
		case 2: /* VLAN filter mask */
		case 3: /* MC filter mask */
			max_index = MAC_ADDR_MAX_CAM_WCOUNT;
			max_offset = MAC_ADDR_MAX_CAM_WCOUNT;
			break;
		case 4: /* FC MAC addresses */
			max_index = MAC_ADDR_MAX_FC_MAC_ENTRIES;
			max_offset = MAC_ADDR_MAX_FC_MAC_WCOUNT;
			break;
		case 5: /* Mgmt MAC addresses */
			max_index = MAC_ADDR_MAX_MGMT_MAC_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_MAC_WCOUNT;
			break;
		case 6: /* Mgmt VLAN addresses */
			max_index = MAC_ADDR_MAX_MGMT_VLAN_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_VLAN_WCOUNT;
			break;
		case 7: /* Mgmt IPv4 address */
			max_index = MAC_ADDR_MAX_MGMT_V4_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_V4_WCOUNT;
			break;
		case 8: /* Mgmt IPv6 address */
			max_index = MAC_ADDR_MAX_MGMT_V6_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_V6_WCOUNT;
			break;
		case 9: /* Mgmt TCP/UDP Dest port */
			max_index = MAC_ADDR_MAX_MGMT_TU_DP_ENTRIES;
			max_offset = MAC_ADDR_MAX_MGMT_TU_DP_WCOUNT;
			break;
		default:
			netdev_err(qdev->ndev, "Bad type!!! 0x%08x\n", type);
			max_index = 0;
			max_offset = 0;
			break;
		}
		for (index = 0; index < max_index; index++) {
			for (offset = 0; offset < max_offset; offset++) {
				val = initial_val
					| (type << MAC_ADDR_TYPE_SHIFT)
					| (index << MAC_ADDR_IDX_SHIFT)
					| (offset);
				qlge_write32(qdev, MAC_ADDR_IDX, val);
				result_index = 0;
				while ((result_index & MAC_ADDR_MR) == 0) {
					result_index = qlge_read32(qdev,
								   MAC_ADDR_IDX);
				}
				result_data = qlge_read32(qdev, MAC_ADDR_DATA);
				*buf = result_index;
				buf++;
				*buf = result_data;
				buf++;
			}
		}
	}
}

static void qlge_get_sem_registers(struct qlge_adapter *qdev, u32 *buf)
{
	u32 func_num, reg, reg_val;
	int status;

	for (func_num = 0; func_num < MAX_SEMAPHORE_FUNCTIONS ; func_num++) {
		reg = MPI_NIC_REG_BLOCK
			| (func_num << MPI_NIC_FUNCTION_SHIFT)
			| (SEM / 4);
		status = qlge_read_mpi_reg(qdev, reg, &reg_val);
		*buf = reg_val;
		/* if the read failed then dead fill the element. */
		if (!status)
			*buf = 0xdeadbeef;
		buf++;
	}
}

/* Create a coredump segment header */
static void qlge_build_coredump_seg_header(struct mpi_coredump_segment_header *seg_hdr,
					   u32 seg_number, u32 seg_size, u8 *desc)
{
	memset(seg_hdr, 0, sizeof(struct mpi_coredump_segment_header));
	seg_hdr->cookie = MPI_COREDUMP_COOKIE;
	seg_hdr->seg_num = seg_number;
	seg_hdr->seg_size = seg_size;
	strncpy(seg_hdr->description, desc, (sizeof(seg_hdr->description)) - 1);
}

/*
 * This function should be called when a coredump / probedump
 * is to be extracted from the HBA. It is assumed there is a
 * qdev structure that contains the base address of the register
 * space for this function as well as a coredump structure that
 * will contain the dump.
 */
int qlge_core_dump(struct qlge_adapter *qdev, struct qlge_mpi_coredump *mpi_coredump)
{
	int status;
	int i;

	if (!mpi_coredump) {
		netif_err(qdev, drv, qdev->ndev, "No memory allocated\n");
		return -EINVAL;
	}

	/* Try to get the spinlock, but dont worry if
	 * it isn't available.  If the firmware died it
	 * might be holding the sem.
	 */
	qlge_sem_spinlock(qdev, SEM_PROC_REG_MASK);

	status = qlge_pause_mpi_risc(qdev);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed RISC pause. Status = 0x%.08x\n", status);
		goto err;
	}

	/* Insert the global header */
	memset(&mpi_coredump->mpi_global_header, 0,
	       sizeof(struct mpi_coredump_global_header));
	mpi_coredump->mpi_global_header.cookie = MPI_COREDUMP_COOKIE;
	mpi_coredump->mpi_global_header.header_size =
		sizeof(struct mpi_coredump_global_header);
	mpi_coredump->mpi_global_header.image_size =
		sizeof(struct qlge_mpi_coredump);
	strncpy(mpi_coredump->mpi_global_header.id_string, "MPI Coredump",
		sizeof(mpi_coredump->mpi_global_header.id_string));

	/* Get generic NIC reg dump */
	qlge_build_coredump_seg_header(&mpi_coredump->nic_regs_seg_hdr,
				       NIC1_CONTROL_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->nic_regs), "NIC1 Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->nic2_regs_seg_hdr,
				       NIC2_CONTROL_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->nic2_regs), "NIC2 Registers");

	/* Get XGMac registers. (Segment 18, Rev C. step 21) */
	qlge_build_coredump_seg_header(&mpi_coredump->xgmac1_seg_hdr,
				       NIC1_XGMAC_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->xgmac1), "NIC1 XGMac Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xgmac2_seg_hdr,
				       NIC2_XGMAC_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->xgmac2), "NIC2 XGMac Registers");

	if (qdev->func & 1) {
		/* Odd means our function is NIC 2 */
		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic2_regs[i] =
				qlge_read32(qdev, i * sizeof(u32));

		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic_regs[i] =
				qlge_read_other_func_reg(qdev, (i * sizeof(u32)) / 4);

		qlge_get_xgmac_regs(qdev, &mpi_coredump->xgmac2[0], 0);
		qlge_get_xgmac_regs(qdev, &mpi_coredump->xgmac1[0], 1);
	} else {
		/* Even means our function is NIC 1 */
		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic_regs[i] =
				qlge_read32(qdev, i * sizeof(u32));
		for (i = 0; i < NIC_REGS_DUMP_WORD_COUNT; i++)
			mpi_coredump->nic2_regs[i] =
				qlge_read_other_func_reg(qdev, (i * sizeof(u32)) / 4);

		qlge_get_xgmac_regs(qdev, &mpi_coredump->xgmac1[0], 0);
		qlge_get_xgmac_regs(qdev, &mpi_coredump->xgmac2[0], 1);
	}

	/* Rev C. Step 20a */
	qlge_build_coredump_seg_header(&mpi_coredump->xaui_an_hdr,
				       XAUI_AN_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xaui_an),
				       "XAUI AN Registers");

	/* Rev C. Step 20b */
	qlge_build_coredump_seg_header(&mpi_coredump->xaui_hss_pcs_hdr,
				       XAUI_HSS_PCS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xaui_hss_pcs),
				       "XAUI HSS PCS Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi_an_hdr, XFI_AN_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xfi_an),
				       "XFI AN Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi_train_hdr,
				       XFI_TRAIN_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xfi_train),
				       "XFI TRAIN Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi_hss_pcs_hdr,
				       XFI_HSS_PCS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xfi_hss_pcs),
				       "XFI HSS PCS Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi_hss_tx_hdr,
				       XFI_HSS_TX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xfi_hss_tx),
				       "XFI HSS TX Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi_hss_rx_hdr,
				       XFI_HSS_RX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xfi_hss_rx),
				       "XFI HSS RX Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi_hss_pll_hdr,
				       XFI_HSS_PLL_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes_xfi_hss_pll),
				       "XFI HSS PLL Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xaui2_an_hdr,
				       XAUI2_AN_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xaui_an),
				       "XAUI2 AN Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xaui2_hss_pcs_hdr,
				       XAUI2_HSS_PCS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xaui_hss_pcs),
				       "XAUI2 HSS PCS Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi2_an_hdr,
				       XFI2_AN_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xfi_an),
				       "XFI2 AN Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi2_train_hdr,
				       XFI2_TRAIN_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xfi_train),
				       "XFI2 TRAIN Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi2_hss_pcs_hdr,
				       XFI2_HSS_PCS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xfi_hss_pcs),
				       "XFI2 HSS PCS Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi2_hss_tx_hdr,
				       XFI2_HSS_TX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xfi_hss_tx),
				       "XFI2 HSS TX Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi2_hss_rx_hdr,
				       XFI2_HSS_RX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xfi_hss_rx),
				       "XFI2 HSS RX Registers");

	qlge_build_coredump_seg_header(&mpi_coredump->xfi2_hss_pll_hdr,
				       XFI2_HSS_PLL_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->serdes2_xfi_hss_pll),
				       "XFI2 HSS PLL Registers");

	status = qlge_get_serdes_regs(qdev, mpi_coredump);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed Dump of Serdes Registers. Status = 0x%.08x\n",
			  status);
		goto err;
	}

	qlge_build_coredump_seg_header(&mpi_coredump->core_regs_seg_hdr,
				       CORE_SEG_NUM,
				       sizeof(mpi_coredump->core_regs_seg_hdr) +
				       sizeof(mpi_coredump->mpi_core_regs) +
				       sizeof(mpi_coredump->mpi_core_sh_regs),
				       "Core Registers");

	/* Get the MPI Core Registers */
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->mpi_core_regs[0],
				   MPI_CORE_REGS_ADDR, MPI_CORE_REGS_CNT);
	if (status)
		goto err;
	/* Get the 16 MPI shadow registers */
	status = qlge_get_mpi_shadow_regs(qdev,
					  &mpi_coredump->mpi_core_sh_regs[0]);
	if (status)
		goto err;

	/* Get the Test Logic Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->test_logic_regs_seg_hdr,
				       TEST_LOGIC_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->test_logic_regs),
				       "Test Logic Regs");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->test_logic_regs[0],
				   TEST_REGS_ADDR, TEST_REGS_CNT);
	if (status)
		goto err;

	/* Get the RMII Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->rmii_regs_seg_hdr,
				       RMII_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->rmii_regs),
				       "RMII Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->rmii_regs[0],
				   RMII_REGS_ADDR, RMII_REGS_CNT);
	if (status)
		goto err;

	/* Get the FCMAC1 Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->fcmac1_regs_seg_hdr,
				       FCMAC1_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->fcmac1_regs),
				       "FCMAC1 Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->fcmac1_regs[0],
				   FCMAC1_REGS_ADDR, FCMAC_REGS_CNT);
	if (status)
		goto err;

	/* Get the FCMAC2 Registers */

	qlge_build_coredump_seg_header(&mpi_coredump->fcmac2_regs_seg_hdr,
				       FCMAC2_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->fcmac2_regs),
				       "FCMAC2 Registers");

	status = qlge_get_mpi_regs(qdev, &mpi_coredump->fcmac2_regs[0],
				   FCMAC2_REGS_ADDR, FCMAC_REGS_CNT);
	if (status)
		goto err;

	/* Get the FC1 MBX Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->fc1_mbx_regs_seg_hdr,
				       FC1_MBOX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->fc1_mbx_regs),
				       "FC1 MBox Regs");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->fc1_mbx_regs[0],
				   FC1_MBX_REGS_ADDR, FC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the IDE Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->ide_regs_seg_hdr,
				       IDE_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->ide_regs),
				       "IDE Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->ide_regs[0],
				   IDE_REGS_ADDR, IDE_REGS_CNT);
	if (status)
		goto err;

	/* Get the NIC1 MBX Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->nic1_mbx_regs_seg_hdr,
				       NIC1_MBOX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->nic1_mbx_regs),
				       "NIC1 MBox Regs");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->nic1_mbx_regs[0],
				   NIC1_MBX_REGS_ADDR, NIC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the SMBus Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->smbus_regs_seg_hdr,
				       SMBUS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->smbus_regs),
				       "SMBus Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->smbus_regs[0],
				   SMBUS_REGS_ADDR, SMBUS_REGS_CNT);
	if (status)
		goto err;

	/* Get the FC2 MBX Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->fc2_mbx_regs_seg_hdr,
				       FC2_MBOX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->fc2_mbx_regs),
				       "FC2 MBox Regs");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->fc2_mbx_regs[0],
				   FC2_MBX_REGS_ADDR, FC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the NIC2 MBX Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->nic2_mbx_regs_seg_hdr,
				       NIC2_MBOX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->nic2_mbx_regs),
				       "NIC2 MBox Regs");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->nic2_mbx_regs[0],
				   NIC2_MBX_REGS_ADDR, NIC_MBX_REGS_CNT);
	if (status)
		goto err;

	/* Get the I2C Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->i2c_regs_seg_hdr,
				       I2C_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->i2c_regs),
				       "I2C Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->i2c_regs[0],
				   I2C_REGS_ADDR, I2C_REGS_CNT);
	if (status)
		goto err;

	/* Get the MEMC Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->memc_regs_seg_hdr,
				       MEMC_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->memc_regs),
				       "MEMC Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->memc_regs[0],
				   MEMC_REGS_ADDR, MEMC_REGS_CNT);
	if (status)
		goto err;

	/* Get the PBus Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->pbus_regs_seg_hdr,
				       PBUS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->pbus_regs),
				       "PBUS Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->pbus_regs[0],
				   PBUS_REGS_ADDR, PBUS_REGS_CNT);
	if (status)
		goto err;

	/* Get the MDE Registers */
	qlge_build_coredump_seg_header(&mpi_coredump->mde_regs_seg_hdr,
				       MDE_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->mde_regs),
				       "MDE Registers");
	status = qlge_get_mpi_regs(qdev, &mpi_coredump->mde_regs[0],
				   MDE_REGS_ADDR, MDE_REGS_CNT);
	if (status)
		goto err;

	qlge_build_coredump_seg_header(&mpi_coredump->misc_nic_seg_hdr,
				       MISC_NIC_INFO_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->misc_nic_info),
				       "MISC NIC INFO");
	mpi_coredump->misc_nic_info.rx_ring_count = qdev->rx_ring_count;
	mpi_coredump->misc_nic_info.tx_ring_count = qdev->tx_ring_count;
	mpi_coredump->misc_nic_info.intr_count = qdev->intr_count;
	mpi_coredump->misc_nic_info.function = qdev->func;

	/* Segment 31 */
	/* Get indexed register values. */
	qlge_build_coredump_seg_header(&mpi_coredump->intr_states_seg_hdr,
				       INTR_STATES_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->intr_states),
				       "INTR States");
	qlge_get_intr_states(qdev, &mpi_coredump->intr_states[0]);

	qlge_build_coredump_seg_header(&mpi_coredump->cam_entries_seg_hdr,
				       CAM_ENTRIES_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->cam_entries),
				       "CAM Entries");
	status = qlge_get_cam_entries(qdev, &mpi_coredump->cam_entries[0]);
	if (status)
		goto err;

	qlge_build_coredump_seg_header(&mpi_coredump->nic_routing_words_seg_hdr,
				       ROUTING_WORDS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->nic_routing_words),
				       "Routing Words");
	status = qlge_get_routing_entries(qdev,
					  &mpi_coredump->nic_routing_words[0]);
	if (status)
		goto err;

	/* Segment 34 (Rev C. step 23) */
	qlge_build_coredump_seg_header(&mpi_coredump->ets_seg_hdr,
				       ETS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->ets),
				       "ETS Registers");
	status = qlge_get_ets_regs(qdev, &mpi_coredump->ets[0]);
	if (status)
		goto err;

	qlge_build_coredump_seg_header(&mpi_coredump->probe_dump_seg_hdr,
				       PROBE_DUMP_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->probe_dump),
				       "Probe Dump");
	qlge_get_probe_dump(qdev, &mpi_coredump->probe_dump[0]);

	qlge_build_coredump_seg_header(&mpi_coredump->routing_reg_seg_hdr,
				       ROUTING_INDEX_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->routing_regs),
				       "Routing Regs");
	status = qlge_get_routing_index_registers(qdev,
						  &mpi_coredump->routing_regs[0]);
	if (status)
		goto err;

	qlge_build_coredump_seg_header(&mpi_coredump->mac_prot_reg_seg_hdr,
				       MAC_PROTOCOL_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->mac_prot_regs),
				       "MAC Prot Regs");
	qlge_get_mac_protocol_registers(qdev, &mpi_coredump->mac_prot_regs[0]);

	/* Get the semaphore registers for all 5 functions */
	qlge_build_coredump_seg_header(&mpi_coredump->sem_regs_seg_hdr,
				       SEM_REGS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header) +
				       sizeof(mpi_coredump->sem_regs),	"Sem Registers");

	qlge_get_sem_registers(qdev, &mpi_coredump->sem_regs[0]);

	/* Prevent the mpi restarting while we dump the memory.*/
	qlge_write_mpi_reg(qdev, MPI_TEST_FUNC_RST_STS, MPI_TEST_FUNC_RST_FRC);

	/* clear the pause */
	status = qlge_unpause_mpi_risc(qdev);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed RISC unpause. Status = 0x%.08x\n", status);
		goto err;
	}

	/* Reset the RISC so we can dump RAM */
	status = qlge_hard_reset_mpi_risc(qdev);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed RISC reset. Status = 0x%.08x\n", status);
		goto err;
	}

	qlge_build_coredump_seg_header(&mpi_coredump->code_ram_seg_hdr,
				       WCS_RAM_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->code_ram),
				       "WCS RAM");
	status = qlge_dump_risc_ram_area(qdev, &mpi_coredump->code_ram[0],
					 CODE_RAM_ADDR, CODE_RAM_CNT);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed Dump of CODE RAM. Status = 0x%.08x\n",
			  status);
		goto err;
	}

	/* Insert the segment header */
	qlge_build_coredump_seg_header(&mpi_coredump->memc_ram_seg_hdr,
				       MEMC_RAM_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->memc_ram),
				       "MEMC RAM");
	status = qlge_dump_risc_ram_area(qdev, &mpi_coredump->memc_ram[0],
					 MEMC_RAM_ADDR, MEMC_RAM_CNT);
	if (status) {
		netif_err(qdev, drv, qdev->ndev,
			  "Failed Dump of MEMC RAM. Status = 0x%.08x\n",
			  status);
		goto err;
	}
err:
	qlge_sem_unlock(qdev, SEM_PROC_REG_MASK); /* does flush too */
	return status;
}

static void qlge_get_core_dump(struct qlge_adapter *qdev)
{
	if (!qlge_own_firmware(qdev)) {
		netif_err(qdev, drv, qdev->ndev, "Don't own firmware!\n");
		return;
	}

	if (!netif_running(qdev->ndev)) {
		netif_err(qdev, ifup, qdev->ndev,
			  "Force Coredump can only be done from interface that is up\n");
		return;
	}
	qlge_queue_fw_error(qdev);
}

static void qlge_gen_reg_dump(struct qlge_adapter *qdev,
			      struct qlge_reg_dump *mpi_coredump)
{
	int i, status;

	memset(&mpi_coredump->mpi_global_header, 0,
	       sizeof(struct mpi_coredump_global_header));
	mpi_coredump->mpi_global_header.cookie = MPI_COREDUMP_COOKIE;
	mpi_coredump->mpi_global_header.header_size =
		sizeof(struct mpi_coredump_global_header);
	mpi_coredump->mpi_global_header.image_size =
		sizeof(struct qlge_reg_dump);
	strncpy(mpi_coredump->mpi_global_header.id_string, "MPI Coredump",
		sizeof(mpi_coredump->mpi_global_header.id_string));

	/* segment 16 */
	qlge_build_coredump_seg_header(&mpi_coredump->misc_nic_seg_hdr,
				       MISC_NIC_INFO_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->misc_nic_info),
				       "MISC NIC INFO");
	mpi_coredump->misc_nic_info.rx_ring_count = qdev->rx_ring_count;
	mpi_coredump->misc_nic_info.tx_ring_count = qdev->tx_ring_count;
	mpi_coredump->misc_nic_info.intr_count = qdev->intr_count;
	mpi_coredump->misc_nic_info.function = qdev->func;

	/* Segment 16, Rev C. Step 18 */
	qlge_build_coredump_seg_header(&mpi_coredump->nic_regs_seg_hdr,
				       NIC1_CONTROL_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->nic_regs),
				       "NIC Registers");
	/* Get generic reg dump */
	for (i = 0; i < 64; i++)
		mpi_coredump->nic_regs[i] = qlge_read32(qdev, i * sizeof(u32));

	/* Segment 31 */
	/* Get indexed register values. */
	qlge_build_coredump_seg_header(&mpi_coredump->intr_states_seg_hdr,
				       INTR_STATES_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->intr_states),
				       "INTR States");
	qlge_get_intr_states(qdev, &mpi_coredump->intr_states[0]);

	qlge_build_coredump_seg_header(&mpi_coredump->cam_entries_seg_hdr,
				       CAM_ENTRIES_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->cam_entries),
				       "CAM Entries");
	status = qlge_get_cam_entries(qdev, &mpi_coredump->cam_entries[0]);
	if (status)
		return;

	qlge_build_coredump_seg_header(&mpi_coredump->nic_routing_words_seg_hdr,
				       ROUTING_WORDS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->nic_routing_words),
				       "Routing Words");
	status = qlge_get_routing_entries(qdev,
					  &mpi_coredump->nic_routing_words[0]);
	if (status)
		return;

	/* Segment 34 (Rev C. step 23) */
	qlge_build_coredump_seg_header(&mpi_coredump->ets_seg_hdr,
				       ETS_SEG_NUM,
				       sizeof(struct mpi_coredump_segment_header)
				       + sizeof(mpi_coredump->ets),
				       "ETS Registers");
	status = qlge_get_ets_regs(qdev, &mpi_coredump->ets[0]);
	if (status)
		return;
}

void qlge_get_dump(struct qlge_adapter *qdev, void *buff)
{
	/*
	 * If the dump has already been taken and is stored
	 * in our internal buffer and if force dump is set then
	 * just start the spool to dump it to the log file
	 * and also, take a snapshot of the general regs
	 * to the user's buffer or else take complete dump
	 * to the user's buffer if force is not set.
	 */

	if (!test_bit(QL_FRC_COREDUMP, &qdev->flags)) {
		if (!qlge_core_dump(qdev, buff))
			qlge_soft_reset_mpi_risc(qdev);
		else
			netif_err(qdev, drv, qdev->ndev, "coredump failed!\n");
	} else {
		qlge_gen_reg_dump(qdev, buff);
		qlge_get_core_dump(qdev);
	}
}
